#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace {

    using json = nlohmann::json;

    constexpr int kTotalDocs = 1'000'000;

    const std::vector<std::string> kWords = {
            "программа", "данные", "поиск", "индекс", "документ", "сервер", "запрос", "результат", "скорость", "тест",
            "система", "файл", "база", "код", "алгоритм", "модуль", "интерфейс", "клиент", "сеть", "память", "москва",
            "россия", "столица", "город", "страна", "история", "культура"
    };

    std::string randomSentence(std::mt19937 &rng, int wordCount) {
        std::uniform_int_distribution<size_t> dist(0, kWords.size() - 1);
        std::string s;
        for (int i = 0; i < wordCount; ++i) {
            if (!s.empty()) {
                s += ' ';
            }
            s += kWords[dist(rng)];
        }

        return s;
    }

    json makeDocumentsBatch(int startId, int count, std::mt19937 &rng) {
        json arr = json::array();
        std::uniform_int_distribution<int> wordsPerDoc(5, 25);
        for (int i = 0; i < count; ++i) {
            arr.push_back({
                {"content", { {"id", startId + i}, {"name", randomSentence(rng, wordsPerDoc(rng))}}}
            });
        }

        return arr;
    }

    const std::string kIndexName = "bench";

    bool postDocuments(httplib::Client &cli, const json &docs, int &received) {
        auto res = cli.Post("/indexes/" + kIndexName + "/documents", docs.dump(), "application/json");
        if (!res) {
            std::println(stderr, "Ошибка загрузки: нет ответа (timeout или соединение)");
            return false;
        }

        if (res->status != 202) {
            std::string msg = res->body.size() > 300 ? res->body.substr(0, 300) + "..." : res->body;
            std::println(stderr, "Ошибка загрузки: HTTP {} body: {}", res->status, msg);
            return false;
        }
        json body = json::parse(res->body.empty() ? "{}" : res->body);
        received = body.value("received", 0);

        return true;
    }

    bool doSearch(httplib::Client &cli, const std::string &query, int limit, std::vector<int64_t> &outTimeMs) {
        json body = {
                {"q",     query},
                {"limit", limit}
        };
        auto t0 = std::chrono::steady_clock::now();
        auto res = cli.Post("/indexes/" + kIndexName + "/search", body.dump(), "application/json");
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        outTimeMs.push_back(static_cast<int64_t>(ms));
        if (!res || res->status != 200) {
            return false;
        }

        return true;
    }

} // namespace

int main() {
    std::println("Бенчмарк загрузки и поиска\n");
    std::println("Параметры: документов = {}\n", kTotalDocs);

    httplib::Client cli("127.0.0.1", 8000);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(1800, 0);
    cli.set_write_timeout(1800, 0);

    std::mt19937 rng(12345);

    std::println("0 -- Создание коллекции \"{}\" (id: int, name: string)", kIndexName);
    json collection_body = {
            {"name",   kIndexName},
            {"fields", json::array({
                    {{"name", "id"},   {"type", "int"}},
                    {{"name", "name"}, {"type", "string"}}
            })}
    };
    auto collection_res = cli.Post("/indexes/collections", collection_body.dump(), "application/json");
    if (!collection_res || (collection_res->status != 201 && collection_res->status != 200)) {
        std::string msg = collection_res && !collection_res->body.empty() ? collection_res->body.substr(0, 200) : "нет ответа";
        std::println(stderr, "Ошибка создания коллекции (status={}): {}", collection_res ? collection_res->status : 0, msg);
        return 1;
    }

    std::println("   Коллекция создана\n");

    std::println("1 -- Загрузка {} документов", kTotalDocs);
    json docs = makeDocumentsBatch(0, kTotalDocs, rng);
    auto tIndexStart = std::chrono::steady_clock::now();
    int received = 0;
    if (!postDocuments(cli, docs, received)) {
        std::println(stderr, "Ошибка загрузки документов");
        return 1;
    }

    auto tIndexEnd = std::chrono::steady_clock::now();
    auto indexMs = std::chrono::duration_cast<std::chrono::milliseconds>(tIndexEnd - tIndexStart).count();
    double indexSec = indexMs / 1000.0;
    std::println("   Итого: {} документов за {:.2f} с ({:.0f} док/с)\n", received, indexSec, (indexSec > 0 ? received / indexSec : 0));

    std::println("2 -- Поиск: 20 запросов по одному и по два слова...");

    std::vector<std::string> queries = {
            "поиск", "документ", "индекс", "сервер", "данные", "москва", "россия",
            "программа код", "база данные", "поиск результат", "индекс документ",
    };
    std::vector<int64_t> searchTimesMs;
    searchTimesMs.reserve(20);
    for (int i = 0; i < 20; ++i) {
        const std::string &q = queries[i % queries.size()];
        if (!doSearch(cli, q, 10, searchTimesMs)) {
            std::println(stderr, "Ошибка поиска по запросу \"{}\"", q);
            return 1;
        }
    }

    int64_t sumMs = 0, minMs = searchTimesMs.empty() ? 0 : searchTimesMs[0], maxMs = 0;
    for (int64_t ms: searchTimesMs) {
        sumMs += ms;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
    }
    double avgMs = searchTimesMs.empty() ? 0 : static_cast<double>(sumMs) / searchTimesMs.size();
    std::println(
            "   Запросов: {}, мин = {} мс, макс = {} мс, среднее = {:.2f} мс\n",
            static_cast<int>(searchTimesMs.size()), minMs, maxMs, avgMs
    );

    return 0;
}
