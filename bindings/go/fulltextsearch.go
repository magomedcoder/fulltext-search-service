package fulltextsearch

/*
#cgo CFLAGS: -I${SRCDIR}/../../src
#cgo LDFLAGS: -L${SRCDIR}/../../build -lfulltextsearch_embed -lstdc++ -lm

#include <stdlib.h>
#include "fulltextsearch_c_api.h"
*/
import "C"

import (
	"encoding/json"
	"errors"
	"fmt"
	"runtime"
	"unsafe"
)

const (
	OK              = C.FULLTEXTSEARCH_OK
	ErrInvalid      = C.FULLTEXTSEARCH_ERR_INVALID
	ErrIndexMissing = C.FULLTEXTSEARCH_ERR_INDEX_NOT_FOUND
	ErrSearch       = C.FULLTEXTSEARCH_ERR_SEARCH
	ErrOOM          = C.FULLTEXTSEARCH_ERR_OOM
)

func Version() string {
	return C.GoString(C.fulltextsearch_version_string())
}

type EngineOptions struct {
	StoragePath      string
	MaxWordLength    int
	StemmingEnabled  bool
	StemmingLanguage string
	DevMode          bool
	StopWordsFile    string
	ConfigBasePath   string
}

type SearchParams struct {
	Query         string
	Phrase        bool
	Partial       bool
	Fuzzy         bool
	FuzzyMaxEdits int
	Limit         int
	Offset        int
}

func DefaultSearchParams() SearchParams {
	return SearchParams{
		Partial:       true,
		FuzzyMaxEdits: 2,
		Limit:         0,
		Offset:        0,
	}
}

type SearchHit struct {
	ID           int             `json:"id"`
	Content      json.RawMessage `json:"content"`
	RankingScore float64         `json:"_rankingScore"`
}

type SearchResponse struct {
	Results          []SearchHit `json:"results"`
	Total            uint64      `json:"total"`
	ProcessingTimeMs int64       `json:"processingTimeMs"`
	Query            string      `json:"query"`
}

type Engine struct {
	ptr *C.fulltextsearch_engine
}

func Open(opts EngineOptions) (*Engine, error) {
	cStorage := C.CString(opts.StoragePath)
	defer C.free(unsafe.Pointer(cStorage))

	var cLang *C.char
	if opts.StemmingLanguage != "" {
		cLang = C.CString(opts.StemmingLanguage)
		defer C.free(unsafe.Pointer(cLang))
	}

	var cStop *C.char
	if opts.StopWordsFile != "" {
		cStop = C.CString(opts.StopWordsFile)
		defer C.free(unsafe.Pointer(cStop))
	}

	var cCfgBase *C.char
	if opts.ConfigBasePath != "" {
		cCfgBase = C.CString(opts.ConfigBasePath)
		defer C.free(unsafe.Pointer(cCfgBase))
	}

	cOpts := C.fulltextsearch_engine_options{
		storage_path:      cStorage,
		max_word_length:   C.int(opts.MaxWordLength),
		stemming_enabled:  boolToCInt(opts.StemmingEnabled),
		stemming_language: cLang,
		dev_mode:          boolToCInt(opts.DevMode),
		stop_words_file:   cStop,
		config_base_path:  cCfgBase,
	}

	ptr := C.fulltextsearch_engine_create(&cOpts)
	if ptr == nil {
		return nil, errors.New("fulltextsearch: fulltextsearch_engine_create не удался (пустой storage_path или ошибка C++)")
	}

	e := &Engine{ptr: ptr}
	runtime.SetFinalizer(e, (*Engine).finalize)
	return e, nil
}

func (e *Engine) finalize() {
	if e.ptr != nil {
		C.fulltextsearch_engine_destroy(e.ptr)
		e.ptr = nil
	}
}

func (e *Engine) Close() {
	if e == nil {
		return
	}

	runtime.SetFinalizer(e, nil)
	if e.ptr != nil {
		C.fulltextsearch_engine_destroy(e.ptr)
		e.ptr = nil
	}
}

func (e *Engine) Search(collection string, p SearchParams, maxLimit, maxOffset int) (*SearchResponse, error) {
	if e == nil || e.ptr == nil {
		return nil, errors.New("fulltextsearch: движок закрыт или не инициализирован")
	}

	cName := C.CString(collection)
	defer C.free(unsafe.Pointer(cName))

	cQuery := C.CString(p.Query)
	defer C.free(unsafe.Pointer(cQuery))

	sp := C.fulltextsearch_search_params{
		collection_name: cName,
		query:           cQuery,
		phrase:          boolToCInt(p.Phrase),
		partial:         boolToCInt(p.Partial),
		fuzzy:           boolToCInt(p.Fuzzy),
		fuzzy_max_edits: C.int(p.FuzzyMaxEdits),
		limit:           C.int(p.Limit),
		offset:          C.int(p.Offset),
	}

	var out *C.char
	var errBuf [512]C.char
	rc := C.fulltextsearch_search_json(
		e.ptr,
		&sp,
		C.int(maxLimit),
		C.int(maxOffset),
		&out,
		&errBuf[0],
		C.size_t(len(errBuf)),
	)

	if rc != C.FULLTEXTSEARCH_OK {
		msg := C.GoString(&errBuf[0])
		if msg == "" {
			msg = fmt.Sprintf("fulltextsearch_search_json код %d", int(rc))
		}

		return nil, fmt.Errorf("%w: %s", codeToErr(int(rc)), msg)
	}
	if out == nil {
		return nil, errors.New("fulltextsearch: fulltextsearch_search_json вернул успех, но json == nil")
	}
	defer C.fulltextsearch_free_string(out)

	var resp SearchResponse
	if err := json.Unmarshal([]byte(C.GoString(out)), &resp); err != nil {
		return nil, err
	}
    
	return &resp, nil
}

func boolToCInt(v bool) C.int {
	if v {
		return 1
	}
	return 0
}

func codeToErr(code int) error {
	switch code {
	case int(ErrInvalid):
		return errors.New("fulltextsearch: неверные аргументы")
	case int(ErrIndexMissing):
		return errors.New("fulltextsearch: коллекция не найдена")
	case int(ErrSearch):
		return errors.New("fulltextsearch: ошибка поиска")
	case int(ErrOOM):
		return errors.New("fulltextsearch: нехватка памяти")
	default:
		return errors.New("fulltextsearch: неизвестная ошибка")
	}
}
