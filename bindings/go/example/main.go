package main

import (
	"fmt"
	"log"
	"os"

	fulltextsearch "github.com/magomedcoder/fulltext-search-service/bindings/go"
)

func main() {
	if len(os.Args) != 4 {
		fmt.Fprintf(os.Stderr, "использование: %s <storage_path> <коллекция> <запрос>\n", os.Args[0])
		os.Exit(1)
	}

	storage := os.Args[1]
	collection := os.Args[2]
	query := os.Args[3]

	eng, err := fulltextsearch.Open(fulltextsearch.EngineOptions{
		StoragePath:      storage,
		StemmingEnabled:  true,
		StemmingLanguage: "russian",
	})
	if err != nil {
		log.Fatal(err)
	}
	defer eng.Close()

	fmt.Println(fulltextsearch.Version())

	p := fulltextsearch.DefaultSearchParams()
	p.Query = query
	p.Limit = 10

	resp, err := eng.Search(collection, p, 0, 0)
	if err != nil {
		log.Fatal(err)
	}

	fmt.Printf("запрос: %q, всего совпадений: %d, время: %d мс\n", resp.Query, resp.Total, resp.ProcessingTimeMs)

	for i, hit := range resp.Results {
		fmt.Printf("[%d] id=%d score=%.4f content=%s\n", i, hit.ID, hit.RankingScore, string(hit.Content))
	}
}
