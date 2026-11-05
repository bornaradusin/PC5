# Test answers

The host tests answer PC5's cover lookups from these files instead of the network.

- `gog-catalog.json`: one product as `catalog.gog.com/v1/catalog?query=like:witcher&limit=2` returned it on
  2026-10-22 (its long lists shortened), inside the `products` list the catalogue answers with.
- The others are written by hand in the shape each source documents (Steam's store search and
  `IStoreBrowseService/GetItems`, MediaWiki's `generator=search` with
  `prop=revisions&rvslots=main` in `formatversion=2` for PCGamingWiki, Wikibase's
  `wbsearchentities` and `wbgetentities` for Wikidata, and SteamGridDB's API v2: `search/autocomplete`,
  `grids` and `heroes`), for made-up games.
