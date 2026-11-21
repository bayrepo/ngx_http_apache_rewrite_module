# Implementation Plans

## Shared Memory Zone for .htaccess Cache

### Problem

Current `.htaccess` cache is bound to per-request pool. Each worker maintains its own independent cache. When `.htaccess` changes on disk, different workers may serve different rule versions during the transition window. While this window is typically milliseconds and rarely causes issues in practice, strict consistency requires shared state.

### Goal

Provide a cross-worker `.htaccess` cache using nginx shared memory zone, so that when one worker detects a file change, all workers immediately see the updated rules on their next request.

### Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                  Shared Memory Zone                  │
│  ┌─────────────────────────────────────────────────┐ │
│  │  RBTree: path (hash) → htaccess_shm_entry_t    │ │
│  │  ┌───────────────────────────────────────────┐  │ │
│  │  │ key: hash of absolute path                │  │ │
│  │  │ mtime: last modification time             │  │ │
│  │  │ rules_hash: hash of serialized rules      │  │ │
│  │  │ generation: monotonic version counter     │  │ │
│  │  │ rules_offset: offset to serialized rules  │  │ │
│  │  │ rules_len: length of serialized blob      │  │ │
│  │  └───────────────────────────────────────────┘  │ │
│  └─────────────────────────────────────────────────┘ │
│  Mutex (ngx_atomic_t / spinlock)                     │
└─────────────────────────────────────────────────────┘
         ▲                    ▲
         │ stat() + lookup    │ update on mtime change
    Worker 1              Worker N
```

### Data Structures

#### Shared memory entry

```c
typedef struct {
    ngx_rbtree_node_t   node;       /* rbtree node (key = path hash) */
    ngx_queue_t         queue;      /* LRU eviction list */
    time_t              mtime;      /* file modification time */
    ngx_uint_t          rules_hash; /* hash of serialized rules blob */
    ngx_uint_t          generation; /* monotonic version counter */
    size_t              rules_offset; /* offset from zone start to rules blob */
    size_t              rules_len;  /* length of serialized rules blob */
    u_char              path[NGX_MAX_PATH]; /* absolute path (null-terminated) */
} htaccess_shm_entry_t;
```

#### Serialized rule format

Rules cannot store `ngx_regex_t*` pointers in shared memory (pointers are process-local). Instead, store the **source pattern strings** and recompile regex on first access per worker.

```c
/* Packed binary format in shared memory: */
typedef struct {
    uint32_t  num_rules;
    uint32_t  flags;           /* global options/state */
    /* Followed by num_rules entries: */
} htaccess_rules_header_t;

typedef struct {
    uint32_t  pattern_len;
    uint32_t  output_len;
    uint32_t  flags;           /* RULEFLAG_* */
    int32_t   skip;
    int32_t   maxrounds;
    int32_t   forced_responsecode;
    uint32_t  num_conds;
    uint32_t  env_len;         /* total env data length */
    uint32_t  mimetype_len;
    /* Followed by: pattern[pattern_len], output[output_len],
       conds[num_conds], env[env_len], mimetype[mimetype_len] */
} htaccess_rule_entry_t;
```

#### Per-worker local cache (optional optimization)

Each worker keeps a small local cache mapping `path_hash → {generation, compiled_rules}`. On cache hit, compare generation with shared memory entry. If generations match — use local compiled rules (no lock, no deserialization). If mismatch — recompile from shared memory blob under lock.

```c
typedef struct {
    ngx_uint_t          path_hash;
    ngx_uint_t          generation;
    ngx_array_t        *compiled_rules;  /* locally compiled regex */
    time_t              mtime;
} htaccess_local_cache_entry_t;
```

### Implementation Steps

#### Phase 1: Shared memory infrastructure

1. Add directive `HtaccessCacheZone <name> <size>` at http/main level
2. In `postconfiguration`, call `ngx_shared_memory_add()` to allocate the zone
3. Initialize zone with:
   - `ngx_rbtree_init()` for path lookup
   - `ngx_queue_init()` for LRU eviction
   - Spinlock or mutex for concurrent access
4. Store zone reference in main config (`ngx_http_apache_rewrite_main_conf_t`)

#### Phase 2: Serialization / deserialization

5. Implement `ngx_htaccess_serialize_rules(pool, rules_array) → ngx_str_t`
   - Walk rule array, pack into contiguous binary blob
   - Store pattern/output strings inline (not pointers)
   - Skip compiled `ngx_regex_t*` — only source strings
6. Implement `ngx_htaccess_deserialize_rules(pool, blob) → ngx_array_t`
   - Unpack binary blob into `ngx_rewrite_rule_t` array
   - Recompile regex from stored pattern strings using `ngx_regex_compile()`
   - Allocate everything from request pool (or worker-local pool)

#### Phase 3: Lookup and update logic

7. Modify `ngx_htaccess_parse_file_from_ha()` flow:
   ```
   a. Compute path_hash from absolute .htaccess path
   b. Lock shared memory
   c. Lookup path_hash in rbtree
   d. If found AND entry.mtime == current_mtime:
      - Unlock
      - Deserialize rules from shared blob (recompile regex)
      - Return compiled rules
   e. If not found OR mtime changed:
      - Parse file as usual (existing code)
      - Serialize new rules
      - Update/insert entry in rbtree
      - Copy serialized blob to shared memory slab
      - Evict LRU entries if slab is full
      - Unlock
      - Return newly parsed rules
   ```
8. Replace current per-request linked-list cache with shared memory lookup
9. Keep per-request pool cleanup for fallback path and other non-shareable data

#### Phase 4: LRU eviction

10. When shared memory slab is full:
    - Evict least-recently-used entry (tail of queue)
    - Free its rules blob from slab
    - Remove from rbtree
11. Move accessed entry to head of queue on each hit

#### Phase 5: Configuration and monitoring

12. Add optional directive `HtaccessCacheZoneSize` (default: 1m)
13. Log cache hits/misses/evictions at debug level
14. Consider adding stub_status-style counters for monitoring

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Regex storage | Source strings only, recompile per worker | `ngx_regex_t*` contains process-local pointers; recompilation is fast (~μs) and happens only on mtime change |
| Synchronization | Spinlock (not mutex) | Critical section is short (rbtree lookup + memcpy); spinlock avoids syscall overhead |
| Eviction policy | LRU | Hot paths stay cached; cold entries evicted automatically |
| Local cache | Generation-based validation | Avoids locking on cache hit; one atomic read of generation counter |
| Serialization format | Custom packed binary | Simpler than protobuf/msgpack; no external dependencies; versioned for forward compatibility |

### Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Shared memory exhaustion | New .htaccess files can't be cached | LRU eviction + configurable zone size + warning log at 80% capacity |
| Regex recompilation latency | First request after change is slower | Acceptable tradeoff; recompilation is microseconds; only on actual file change |
| Stale local cache | Worker uses old rules briefly | Generation check on every access; worst case = one request with old rules |
| Lock contention under high load | Spinlock spinning wastes CPU | Critical section is <1μs; consider reader-writer lock if profiling shows contention |
| Backward compatibility | Existing configs without zone directive | Fall back to current per-request cache when zone is not configured |

### Testing Plan

1. Unit test serialization round-trip: serialize → deserialize → compare rules
2. Integration test: modify .htaccess during concurrent requests, verify all workers pick up changes
3. Stress test: 100+ concurrent requests with frequent .htaccess modifications
4. Memory test: fill zone to capacity, verify LRU eviction works correctly
5. Regression: ensure behavior without `HtaccessCacheZone` directive is unchanged

### Dependencies

- No external libraries
- Uses nginx core APIs: `ngx_shared_memory_add`, `ngx_slab_alloc`, `ngx_rbtree`, `ngx_queue`, `ngx_spinlock`
- Compatible with nginx 1.22+ (current minimum supported version)

### Estimated Effort

| Phase | Lines of Code | Complexity |
|-------|--------------|------------|
| Phase 1: Infrastructure | ~200 | Medium |
| Phase 2: Serialization | ~300 | High |
| Phase 3: Lookup/update | ~250 | Medium |
| Phase 4: LRU eviction | ~80 | Low |
| Phase 5: Config/monitoring | ~100 | Low |
| Tests | ~400 | Medium |
| **Total** | **~1330** | |

---

## Hierarchical .htaccess Merging (Apache-Compatible)

### Problem

When a subdirectory contains its own `.htaccess` file, the module currently uses **only that file's rules**, ignoring the parent/root `.htaccess`. In Apache mod_rewrite, `.htaccess` files are processed **hierarchically**: rules from the root `.htaccess` are applied first, then subdirectory rules are appended. This means security rules defined in the root `.htaccess` (e.g., blocking `user/data/*.json`) apply even when a subdirectory has its own `.htaccess` with different rules.

**Real-world impact:** Grav CMS places a secondary `.htaccess` in `user/data/` using `mod_alias` (`RedirectMatch`). Our module finds this subdir `.htaccess` first, ignores the root `.htaccess` Security section, and serves private files that should be blocked.

### Goal

Merge rules from all `.htaccess` files along the path from docroot to the request directory, applying them in Apache order: root first, then each subdirectory level down to the leaf. Subdirectory `.htaccess` can override `RewriteEngine`, `RewriteBase`, `RewriteOptions`, and `RewriteFallBack`; rules are always cumulative.

### Why Previous Attempt Failed

First implementation attempt caused infinite internal redirect loops. Root cause: merging root `.htaccess` Index rule (`RewriteRule .* index.php [L]`) with subdir rules caused requests for non-existing files to rewrite to `index.php`, which triggered another handler invocation, which merged again, creating a cycle. The `!r->internal` guard was insufficient because nginx creates new subrequests (not marked as internal) for some redirect types.

### Architecture

```
Request: /user/data/file.json
DocRoot: /var/www/site/

Step 1: Collect .htaccess paths (docroot → leaf):
  [0] /var/www/site/.htaccess           (root — Security, Index, Exploits)
  [1] /var/www/site/user/.htaccess       (if exists)
  [2] /var/www/site/user/data/.htaccess  (leaf — mod_alias rules)

Step 2: Parse each file (with caching per path+mtime)

Step 3: Merge rules in order: root_rules + user_rules + data_rules

Step 4: Apply merged rule set once via ngx_rewrite_apply_list()

Step 5: Use RewriteBase/RewriteOptions/RewriteFallBack from DEEPEST .htaccess
```

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Collection direction | Docroot → leaf (top-down) | Matches Apache processing order |
| Rule merging | Concatenate all rules into single array | Single pass through `apply_list`, avoids multiple handler invocations |
| Config inheritance | Deepest .htaccess wins for state/baseurl/fallback/options | Matches Apache per-directory override semantics |
| Caching | Per-path cache entry (existing mechanism) | Each .htaccess cached independently by mtime |
| Loop prevention | Apply merged rules in single `apply_list` call | No re-entry, no recursive handler calls |
| `<IfModule>` handling | Skip unknown directives (current behavior) | `mod_alias` directives ignored; only `mod_rewrite` rules merged |

### Implementation Steps

#### Phase 1: Path collection function

Create `ngx_htaccess_collect_paths()` that walks from docroot to current directory, collecting all existing `.htaccess` file paths into an `ngx_array_t`:

```c
static ngx_array_t *
ngx_htaccess_collect_paths(ngx_http_request_t *r,
    u_char *docroot, size_t docroot_len,
    ngx_str_t *current_path,
    u_char *htaccess_name, size_t htaccess_name_len);
```

- Start at docroot, check for `.htaccess`
- Walk each path component toward current directory
- For each level, stat() the `.htaccess` file
- Return array of `ngx_str_t` paths in top-down order
- Return NULL if no `.htaccess` files found

**Critical:** Must handle edge cases:
- docroot == current directory (single level)
- Trailing slash variations
- Variable roots (`$root_path`)
- Empty path components

#### Phase 2: Multi-file parse and merge

Replace single-file parse in location handler with loop over collected paths:

```c
for each htaccess_path in collected_paths:
    1. Check cache (existing htaccess_cache_head linked list)
    2. If not cached: parse via ngx_htaccess_parse_file_from_ha()
    3. Append rules to merged_rules array
    4. Track state/baseurl/options/fallback from deepest file
```

**Loop prevention:** All rules merged into single array BEFORE calling `apply_list`. No recursive handler invocations. No re-parsing on internal redirects.

#### Phase 3: Cache integration

Extend existing cache to support multiple entries per request:
- Current cache: linked list keyed by file path + mtime
- Already supports multiple entries (linked list structure)
- Each `.htaccess` level cached independently
- On mtime change, only affected level re-parsed

#### Phase 4: Testing

1. Create test fixture with nested `.htaccess`:
   - Root: Security rules (block `user/data/*.json`)
   - `user/data/`: Separate `.htaccess` with different rules
   - Verify root Security rules still apply to `user/data/` files
2. Test with Grav CMS real `.htaccess` structure
3. Verify no performance regression for sites without nested `.htaccess`
4. Verify cache works correctly across multiple levels

### Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Infinite redirect loop | Server hangs | Single `apply_list` call with pre-merged rules; no recursive handler entry |
| Performance: multiple stat() calls | Slower requests | Cache per path+mtime; stat() only on cache miss |
| Performance: parsing multiple files | Slower first request | Cache parsed rules; subsequent requests use cache |
| Rule ordering conflicts | Unexpected behavior | Document Apache-compatible ordering; deepest .htaccess config wins |
| Memory: multiple parsed rule sets | Higher pool usage | Rules cached and shared across requests via pool cleanup |
| `<IfModule>` wrapping | Rules inside unsupported modules skipped | Acceptable: only `mod_rewrite` rules processed; document limitation |

### Estimated Effort

| Phase | Lines of Code | Complexity |
|-------|--------------|------------|
| Phase 1: Path collection | ~80 | Medium |
| Phase 2: Multi-file parse/merge | ~100 | High |
| Phase 3: Cache integration | ~30 | Low |
| Phase 4: Testing | ~100 | Medium |
| **Total** | **~310** | |

### Dependencies

- Existing `ngx_htaccess_parse_file_from_ha()` — reused as-is
- Existing cache infrastructure (`htaccess_entry_t` linked list) — extended
- Existing `ngx_htaccess_build_path()` — reused for initial path construction
- No new external dependencies
