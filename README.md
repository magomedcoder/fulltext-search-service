# FullTextSearchService

Сервис полнотекстового поиска с HTTP API

### Реализовано

- [x] Загрузка документов, пересборка индекса
- [x] Список документов с пагинацией
- [x] Полнотекстовый поиск с ранжированием
- [x] Персистентность индекса
- [x] Конфигурация через конфиг-файл и systemd конфиг
- [x] Логирование (флаг --dev) и Dockerfile
- [x] Ранжирование BM25
- [x] Rate limiting и лимиты размера запросов
- [x] Схема документов
- [x] Документы как объект по схеме
- [x] Индексация для поиска только по строковым полям схемы
- [x] Коллекции по имени
- [x] Подсветка совпадений (snippets/highlight) в результатах поиска
- [x] Стемминг (нормализация словоформ) через Snowball
- [x] Поиск без учёта регистра

### Планируется

- [ ] Стоп-слова при индексации и в запросе
- [ ] Фразовый поиск (поиск точной фразы)
- [ ] Нечёткий поиск (fuzzy, поиск с опечатками)
- [ ] Инкрементальное обновление индекса
- [ ] Метаданные документов

## Сборка на хосте

#### Зависимости

- C++ 23
- GCC 14
- CMake 3.22
- Git (для загрузки сторонних библиотек через FetchContent)
- make (по умолчанию) или ninja (опционально: cmake -G Ninja -B build)

При первой конфигурации CMake скачивает исходники зависимостей (nlohmann/json, cpp-httplib, yaml-cpp, libstemmer)

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

Без опции `--config` конфиг читается из `/etc/fulltext-search-service/config.yaml`
 при отсутствии или ошибке файла сервис завершается с ошибкой

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

С сохранением индекса на хосте

```bash
docker run --rm -p 8000:8000 -v $(pwd)/data:/var/lib/fulltext-search-service fulltext-search-service
```

## API

Все операции привязаны к имени коллекции `name`

Коллекции изолированы: у каждой свои поля и свои документы

Имя коллекции в пути - латиница, цифры, `_`, `-` (до 256 символов)

### Коллекции

Поддерживаются типы полей: `int`, `string`

Строковые поля индексируются для полнотекстового поиска

#### Создать коллекцию

```bash
curl -X POST http://127.0.0.1:8000/indexes/collections \
  -H 'Content-Type: application/json' \
  -d '{"name": "products", "fields": [{"name": "id", "type": "int"}, {"name": "name", "type": "string"}]}'
```

#### Список коллекций

```bash
curl http://127.0.0.1:8000/indexes/collections
```

#### Получить коллекцию по имени

```bash
curl http://127.0.0.1:8000/indexes/collections/products
```

#### Удалить коллекцию по имени

```bash
curl -X DELETE http://127.0.0.1:8000/indexes/collections/products
```

---

#### Загрузка документов

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/documents \
  -H 'Content-Type: application/json' \
  -d '[{"content": {"id": 1, "name": "Первый документ"}}, {"content": {"id": 2, "name": "Второй документ"}}]'
```

| Поле      | Тип    | Описание                        |
|-----------|--------|---------------------------------|
| `content` | object | Объект по схеме (поля из схемы) |

Пример ответа

```json
{
  "received": 2
}
```

### Список документов

```bash
curl http://127.0.0.1:8000/indexes/products/documents?limit=10&offset=0
```

| Параметр | По умолчанию  |
|----------|---------------|
| `limit`  | 20  (до 1000) |
| `offset` | 0             |

Пример ответа

```json
{
  "results": [
    {
      "id": 0,
      "content": {
        "id": 1,
        "name": "Первый документ"
      }
    },
    {
      "id": 1,
      "content": {
        "id": 2,
        "name": "Второй документ"
      }
    }
  ],
  "limit": 10,
  "offset": 0,
  "total": 2
}
```

---

### Поиск

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "limit": 5, "offset": 0}'
```

| Параметр    | Тип             | По умолчанию |
|-------------|-----------------|--------------|
| `q`         | string          | `""`         |
| `limit`     | int             | 20 (до 100)  |
| `offset`    | int             | 0            |
| `highlight` | bool или object | `false`      |

Подсветка совпадений (snippets/highlight): при `"highlight": true` в каждом результате добавляются поля

- **highlight** копия `content`, в которой во всех строковых полях вхождения слов из запроса обёрнуты в
  теги `<em>...</em>` (целые слова по пробелам)
- **snippet** одна строка - объединённый текст из строковых полей с подсветкой,
  обрезанный до заданной длины с суффиксом (по умолчанию 255 символов и `...`)

Кастомные настройки: передайте объект вместо `true`:
- **pre**, **post** - теги подсветки, например `"highlight": {"pre": "<mark>", "post": "</mark>"}`
- **snippet_length** - максимальная длина сниппета в символах (по умолчанию 255)
- **snippet_suffix** - суффикс при обрезке (по умолчанию `"..."`)

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "limit": 5, "highlight": {"snippet_length": 255, "snippet_suffix": "..."}}'
```

Пример ответа

```json
{
  "results": [
    {
      "id": 0,
      "content": {
        "id": 2,
        "name": "Второй документ"
      },
      "_rankingScore": 0.95
    }
  ],
  "limit": 5,
  "offset": 0,
  "total": 1,
  "processingTimeMs": 2,
  "query": "Второй"
}
```

- **id** порядковый номер документа в коллекции (индекс: 0, 1, 2,...)
- **\_rankingScore** - оценка по BM25 (учитываются tf, idf, длина документа), нормализована в [0, 1]
- **processingTimeMs** - время обработки запроса на сервере в миллисекундах

Пример результата с подсветкой (`"highlight": true`)

```json
{
  "id": 0,
  "content": {
      "id": 2,
      "name": "Второй документ"
  },
  "_rankingScore": 0.95,
  "highlight": {
      "id": 2,
      "name": "<em>Второй</em> документ"
  },
  "snippet": "<em>Второй</em> документ"
}
```

Пример запроса с кастомными настройками

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "limit": 5, "highlight": {"pre": "<mark>", "post": "</mark>", "snippet_length": 255, "snippet_suffix": "..."}}'
```
