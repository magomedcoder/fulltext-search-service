# FullTextSearchService

Сервис полнотекстового поиска с HTTP API

### Реализовано

- [x] Загрузка документов, пересборка индекса (многопоточная индексация)
- [x] Список документов с пагинацией
- [x] Полнотекстовый поиск с ранжированием (TF, нормализация, сортировка)
- [x] Персистентность индекса (docs.dat, dict.dat, сохранение/загрузка)
- [x] Конфигурация через конфиг-файл и systemd конфиг
- [x] Логирование (флаг --dev) и Dockerfile (сборка образа и запуск)
- [x] Ранжирование BM25 (улучшение качества поиска)

### Планируется

- [ ] Rate limiting и лимиты размера запросов
- [ ] Стоп-слова при индексации и в запросе
- [ ] Стемминг или лемматизация (нормализация словоформ)
- [ ] Подсветка совпадений (snippets/highlight) в результатах поиска
- [ ] Фразовый поиск (поиск точной фразы)
- [ ] Нечёткий поиск (fuzzy, поиск с опечатками)
- [ ] Инкрементальное обновление индекса (добавление/удаление документов без полной пересборки)
- [ ] Метаданные документов (поля title, date и фильтрация по ним)

## Сборка на хосте

#### Зависимости

- C++ 23
- GCC 14
- CMake 3.22
- Git (для загрузки сторонних библиотек через FetchContent)
- make или ninja (система сборки обычно входит в build-essential)

При первой конфигурации CMake скачивает исходники зависимостей (nlohmann/json, cpp-httplib, yaml-cpp) в каталог
third_party

#### Сборка

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Запуск

```bash
./build/fulltext-search-service
# или
./build/fulltext-search-service --config=config.yaml
```

Без опции --config конфиг обязательно читается из /etc/fulltext-search-service/config.yaml
при отсутствии или ошибке файла сервис завершается с ошибкой

```bash
./build/fulltext-search-service --config=config.yaml
```

```bash
sudo mkdir -p /var/lib/fulltext-search-service /etc/fulltext-search-service
sudo cp config.yaml /etc/fulltext-search-service/config.yaml

sudo cp build/fulltext-search-service /usr/local/bin/

sudo cp systemd/fulltext-search-service.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now fulltext-search-service
```

### Docker

Получить бинарник

```bash
docker build --target binary -t fulltext-search-service-binary .
mkdir -p build
docker run --rm -v $(pwd)/build:/output fulltext-search-service-binary
```

Собрать образ и запустить сервис

```bash
docker build -t fulltext-search-service .
docker run --rm -p 8000:8000 fulltext-search-service
```

Запуск с сохранением индекса на хосте

```bash
docker run --rm -p 8000:8000 -v $(pwd)/data:/var/lib/fulltext-search-service fulltext-search-service
```

## API

### Загрузка документов

```bash
curl -X POST 'http://127.0.0.1:8000/indexes/documents' \
  -H 'Content-Type: application/json' \
  -d '[{"content": "первый документ"}, {"content": "второй документ"}]'
```

Индекс пересобирается по загруженному набору

| Поле      | Тип    | Описание        |
|-----------|--------|-----------------|
| `content` | string | Текст документа |

Пример ответа

```json
{
    "received": 2
}
```

### Список документов

```bash
curl 'http://127.0.0.1:8000/indexes/documents?offset=0&limit=10'
```

| Параметр | По умолчанию | Описание |
|----------|--------------|----------|
| `limit`  | int          | 20       | до 100   |
| `offset` | int          | 0        |          |

Пример ответа

```json
{
    "results": [
        {
            "id": 0,
            "content": "текст первого документа"
        },
        {
            "id": 1,
            "content": "текст второго документа"
        }
    ],
    "limit": 10,
    "offset": 0,
    "total": 2
}
```

### Поиск

```bash
curl -X POST 'http://127.0.0.1:8000/indexes/search' \
  -H 'Content-Type: application/json' \
  -d '{"q": "второго", "limit": 5}'
```

| Параметр | Тип    | По умолчанию | Описание |
|----------|--------|--------------|----------|
| `q`      | string | `""`         |          |
| `limit`  | int    | 20           | до 100   |
| `offset` | int    | 0            |          |

Пример ответа

```json
{
    "hits": [
        {
            "id": 0,
            "content": "текст второго документа",
            "_rankingScore": 1.0
        }
    ],
    "limit": 5,
    "offset": 0,
    "estimatedTotalHits": 1,
    "processingTimeMs": 0,
    "query": "второго"
}
```
