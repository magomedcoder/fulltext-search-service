# FullTextSearchService

### Зависимости

- C++ 23
- GCC 14
- CMake 3.22

## Сборка и Запуск

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

./build/fulltext-search-service
```

## API

### Загрузка документов

```bash
curl -X POST 'http://127.0.0.1:8000/indexes/documents' \
  -H 'Content-Type: application/json' \
  -d '[{"content": "первый документ"}, {"content": "второй документ"}]'
```

### Список документов

```bash
curl 'http://127.0.0.1:8000/indexes/documents?offset=0&limit=10'
```

### Поиск

```bash
curl -X POST 'http://127.0.0.1:8000/indexes/search' \
  -H 'Content-Type: application/json' \
  -d '{"q": "первый документ", "limit": 5}'
```
