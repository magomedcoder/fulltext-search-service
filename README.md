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
- [x] Обрезка полей - фрагмент текста вокруг совпадения
- [x] Фразовый поиск (поиск точной фразы)
- [x] Нечёткий поиск (fuzzy, поиск с опечатками)
- [x] Поиск по подстроке (partial, из любой части слова)

### Планируется

- [ ] Стоп-слова при индексации и в запросе
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
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_STATIC=ON
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

Получить статический бинарник

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

| Параметр               | Тип              | По умолчанию |
|------------------------|------------------|--------------|
| `q`                    | string           | `""`         |
| `limit`                | int              | 20 (до 100)  |
| `offset`               | int              | 0            |
| `highlight`            | bool или object  | `false`      |
| `crop_fields`          | array или string | -            |
| `crop_length`          | int              | 15           |
| `crop_marker`          | string           | `"..."`      |
| `attributesToRetrieve` | array или string | все поля     |
| `phrase`               | bool             | `false`      |
| `partial`              | bool             | `true`       |
| `fuzzy`                | bool             | `false`      |
| `fuzzy_max_edits`      | int              | 2 (0–3)      |

**Поиск по подстроке (partial):** по умолчанию включён. Для терминов запроса, отсутствующих в индексе как целое слово, находятся документы, в которых есть слова, **содержащие** запрос как подстроку (из любой части слова). Например, запрос «док» находит «документ», «документация»; «кумент» находит «документ». Ранг таких совпадений немного снижается относительно точного. Чтобы искать только по точному совпадению слов, передайте `"partial": false`.

**Нечёткий поиск (fuzzy):** при `"fuzzy": true` для терминов запроса, отсутствующих в индексе, подбираются близкие по расстоянию Левенштейна (до `fuzzy_max_edits` правок: вставка, удаление, замена символа). Ранг документа снижается пропорционально числу правок. Подсветка при включённом fuzzy использует фактически найденные индексные термины.

**Фразовый поиск:** при `"phrase": true` запрос трактуется как точная фраза - возвращаются только документы, в которых все слова из `q` идут подряд в том же порядке

Применяется та же нормализация (стемминг, регистр), что и при обычном поиске

Ранг документов с найденной фразой - 1.0

**Форматированное представление (\_formatted):** при включении подсветки (`highlight`) и/или обрезки (`crop_fields`) в каждом результате добавляется объект **\_formatted** - единое представление полей для отображения

- Поля из **crop_fields** - обрезаны до `crop_length` слов вокруг первого совпадения, в местах обрезки подставляется **crop_marker**
  Если включена подсветка, в обрезанном тексте совпадения оборачиваются в теги
- Остальные строковые поля - при включённой подсветке в них совпадения оборачиваются в теги `<em>...</em>` (или кастомные pre/post)

Кастомные настройки подсветки (объект `highlight`)

- **pre**, **post** - теги подсветки, например `"highlight": {"pre": "<mark>", "post": "</mark>"}`
- **snippet_length** - максимальная длина поля **snippet** в символах (по умолчанию 255)
- **snippet_suffix** - суффикс при обрезке сниппета (по умолчанию `"..."`)

При `"highlight": true` дополнительно возвращается **snippet** - одна строка, объединённый текст из строковых полей с подсветкой

**Проекция:** при указании **attributesToRetrieve** (массив имён полей) в `content` каждого результата возвращаются только эти поля; при пустом или отсутствующем параметре - весь документ

Параметры обрезки

- **crop_fields** - массив имён полей для обрезки (например `["name"]`)
- **crop_length** - число слов вокруг совпадения (по умолчанию 15)
- **crop_marker** - строка в начале/конце обрезанного фрагмента (по умолчанию `"..."`)

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "limit": 5, "highlight": {"snippet_length": 255, "snippet_suffix": "..."}}'
```

Пример запроса с обрезкой полей

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "crop_fields": ["name"], "crop_length": 15, "crop_marker": "..."}'
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

Пример результата с подсветкой и обрезкой (`"highlight": true`, `"crop_fields": ["name"]`)

```json
{
  "id": 0,
  "content": {
    "id": 2,
    "name": "Второй документ"
  },
  "_rankingScore": 0.95,
  "_formatted": {
    "name": "... <em>Второй</em> документ ..."
  },
  "snippet": "<em>Второй</em> документ"
}
```

В **\_formatted** поля из `crop_fields` возвращаются обрезанными (с маркером в местах обрезки), при включённой подсветке совпадения обёрнуты в теги

Остальные строковые поля - с подсветкой без обрезки

Пример запроса с кастомными настройками

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй", "limit": 5, "highlight": {"pre": "<mark>", "post": "</mark>", "snippet_length": 255, "snippet_suffix": "..."}}'
```

Пример запроса с фразовым поиском (точная фраза)

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Второй документ", "phrase": true, "limit": 5}'
```

Пример запроса по подстроке (поиск из любой части слова)

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "док", "partial": true, "limit": 5}'
```

Пример запроса с нечётким поиском (поиск с опечатками)

```bash
curl -X POST http://127.0.0.1:8000/indexes/products/search \
  -H 'Content-Type: application/json' \
  -d '{"q": "Вторй документ", "fuzzy": true, "fuzzy_max_edits": 2, "limit": 5}'
```
