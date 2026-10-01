# Offline dictionaries

SumatraPDF Enhanced uses the same offline vocabulary dictionaries as WMKeyboard: eleven English packs built from Wiktionary through kaikki.org, with WordNet definitions where needed. All their bundled definition senses are available offline. Installed WMKeyboard packs take precedence, followed by the bundled WM definitions; Princeton WordNet 3.0 supplies wider English fallback coverage. Definitions come from WordNet, developed by Princeton University; they are not original SumatraPDF content. The full redistribution license accompanies the files at `src/dictionaries/wordnet-en/LICENSE` and must remain in distributed dictionary folders.

## Sources and attribution

- [Princeton WordNet](https://wordnet.princeton.edu/) is the authoritative project.
- [License and commercial use](https://wordnet.princeton.edu/license-and-commercial-use) describes the license.
- The data and exception files come from the [NLTK WordNet mirror](https://github.com/nltk/wordnet/tree/ce91915ae38a341ae845be4d825ef6003cddf395/wn/data/wordnet-3.0), pinned to revision `ce91915ae38a341ae845be4d825ef6003cddf395`.
- `src/dictionaries/wordnet-en/manifest.json` records the bundled file sizes and SHA-256 digests. Runtime downloads use the pinned revision and validate parsing before installation; runtime checksum verification is not currently performed.

Package `src/dictionaries` beside the executable as `dictionaries`. Keep all four `data.*` files, four `*.exc` files, `LICENSE`, and the manifest. The four WordNet data files contain 117,659 synsets. Multiple headwords and senses are retained. Irregular forms such as `went` and `better` use the exception lists; common plural, past-tense and progressive endings also have fallback rules.

## Install and download

Lookup never performs network requests. The dictionary manager makes downloads only after a user chooses Download. The catalog starts with the eleven original WMKeyboard packs. Download stores each complete `.wmvocab.json.gz` pack, including its original attribution, in the user dictionary folder. The bundled meanings remain available when a downloaded copy is removed. WordNet is an additional fallback catalog entry; Download restores its data into the user dictionary folder. See [vocabulary attribution](../vocabulary-attribution.md) and `data/vocabulary/pack-attributions.json` for original pack sources and licenses. Additional languages can be installed from locally acquired, appropriately licensed StarDict or UTF-8 TSV dictionaries.

The writable folder is `dictionaries` beside the active settings file. In portable mode, copy that folder along with the executable and settings. The engine first tries an installed WordNet copy, then the executable's bundled copy. Removing a downloaded English copy leaves the bundled dictionary available. Imported dictionary licenses remain the responsibility of the dictionary supplier and must accompany redistributed packs.

To import a WMKeyboard vocabulary pack, choose a `.wmvocab.json` or `.wmvocab.json.gz` file. The engine accepts format `wmkeyboard-vocab`, version 1, UTF-8 words and all definition senses, with each sense's part of speech. It preserves the original complete JSON file and its attribution on disk. Network downloads fetch the upstream WMKeyboard pack files only after the user chooses Download.

To import StarDict, choose its `.ifo` file with companion files in the same folder. Supported companions are `.idx` or `.idx.gz`, `.dict` or `.dict.dz`, and optional `.syn`. The engine reads 32-bit offsets and UTF-8 text fields, including plain text and HTML-like definition fields. It strips markup and displays plain text, without executing HTML. Audio, images, 64-bit index offsets and non-UTF-8 legacy encodings are not supported. A successful import copies the companion files into the writable dictionary folder.

TSV uses UTF-8, optionally with a BOM. Each nonempty line is a headword, one tab, then its definition. Lines starting with `#` are comments. Duplicate headwords can supply multiple meanings. Example:

```text
photosynthesis	The process by which plants use light to synthesize nutrients.
```

## Engine and ownership

`OfflineDictionary.h` exposes synchronous lookup, catalog, import, download and remove operations. Run file/network operations on a worker; the dialog posts completion back to the UI. A recursive mutex protects the cached index and a separate mutex serializes installations. Resetting the cache after successful modifications causes the next lookup to rebuild its index.

`OfflineMeaning.dictionary` is the display title; `dictionaryId` is `wmkeyboard-vocab-en` for WM meanings or a stable pack ID such as `wordnet-en` for other dictionaries. WM catalog management IDs are `wm-<sourceId>`, separate from the shared meaning ID used by learning decks. Imported IDs derive from the file basename. Call `FreeOfflineMeanings` and `FreeDictionaryCatalog` for their owned strings. Error strings returned through `Str* error` are caller-owned and released with `str::Free`.

Queries and index keys use invariant Unicode case folding, surrounding punctuation trimming, and underscore-to-space normalization. Exact matches precede English inflection fallback. Lookup returns up to 64 distinct meanings. Bounds are 256 MiB per input file or decompressed input, two million entries, 512 bytes per headword and 256 KiB per definition. Malformed imported files are rejected on installation and skipped while loading the catalog's dictionary files.

Debug unit tests cover duplicate keys, Unicode case folding, missing TSV separators, invalid UTF-8, truncated StarDict indexes, invalid offsets and sizes, unsupported offset widths, malformed WordNet records, and WMKeyboard JSON format/sense parsing. The application test runner calls `OfflineDictionary_UnitTests`.
