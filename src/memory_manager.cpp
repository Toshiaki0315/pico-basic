#include "parser_internal.h"
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------
// Shared State Definitions
// ---------------------------------------------------------
uint8_t logical_memory[65536];

uint16_t string_heap_ptr = STRING_HEAP_BASE;
uint16_t array_heap_inner_ptr = DATA_HEAP_BASE;

// ---------------------------------------------------------
// Memory Compact Storage Helpers
// ---------------------------------------------------------

static void write_compact_value(uint16_t addr, const Value& val) {
    if (val.type == Value::Type::STR) {
        uint8_t old_type   = logical_memory[addr + 8];
        uint8_t old_active = logical_memory[addr + 9];
        if (old_active && (Value::Type)old_type == Value::Type::STR) {
            uint16_t old_str_ptr;
            memcpy(&old_str_ptr, &logical_memory[addr + 10], 2);
            if (old_str_ptr >= STRING_HEAP_BASE && old_str_ptr < string_heap_ptr) {
                uint16_t old_alloc;
                memcpy(&old_alloc, &logical_memory[old_str_ptr], 2);
                int new_len = (int)strlen(val.str_val);
                if (old_alloc > 0 && (uint16_t)(new_len + 1) <= old_alloc) {
                    memcpy(&logical_memory[old_str_ptr + 2], val.str_val, (size_t)(new_len + 1));
                    logical_memory[addr + 8] = (uint8_t)val.type;
                    return;
                }
            }
        }
        uint16_t str_ptr = string_heap_ptr;
        int len = (int)strlen(val.str_val);
        uint16_t alloc_size = (uint16_t)(len + 1);
        if ((uint32_t)str_ptr + 2 + alloc_size >= 65536u)
            throw std::runtime_error("String Heap Overflow");
        logical_memory[str_ptr]     = (uint8_t)(alloc_size & 0xFF);
        logical_memory[str_ptr + 1] = (uint8_t)(alloc_size >> 8);
        memcpy(&logical_memory[str_ptr + 2], val.str_val, (size_t)alloc_size);
        memcpy(&logical_memory[addr + 10], &str_ptr, 2);
        string_heap_ptr = (uint16_t)(str_ptr + 2 + alloc_size);
        logical_memory[addr + 8] = (uint8_t)val.type;
        return;
    }
    logical_memory[addr + 8] = (uint8_t)val.type;
    if (val.type == Value::Type::NUM) {
        memcpy(&logical_memory[addr + 12], &val.num_val, 4);
    } else if (val.type == Value::Type::INT) {
        memcpy(&logical_memory[addr + 12], &val.int_val, 4);
    }
}

static Value read_compact_value(uint16_t addr) {
    Value v;
    v.type = (Value::Type)logical_memory[addr + 8];
    if (v.type == Value::Type::NUM) {
        memcpy(&v.num_val, &logical_memory[addr + 12], 4);
    } else if (v.type == Value::Type::INT) {
        memcpy(&v.int_val, &logical_memory[addr + 12], 4);
        v.num_val = (float)v.int_val;
    } else if (v.type == Value::Type::STR) {
        uint16_t str_ptr;
        memcpy(&str_ptr, &logical_memory[addr + 10], 2);
        strncpy(v.str_val, (const char*)&logical_memory[str_ptr + 2], 127);
        v.str_val[127] = '\0';
    }
    return v;
}

// ---------------------------------------------------------
// 変数名 → スロット番号のハッシュ索引。
//
// 変数は 16 バイト固定スロットに直線状に並ぶ。ループ内で同じ変数を何度も
// 引くため、線形走査＋strncmp をやめてハッシュで O(1) 参照する。
// スロットは個別削除されない（NEW/CLEAR で一括消去）ので、線形プローブで
// 空きに当たれば「存在しない」と確定できる。索引は遅延構築し、CLEAR で無効化。
// ---------------------------------------------------------
#define VAR_HASH_SIZE 256  // MAX_VARIABLES(128) の 2 倍、2 のべき乗
static int16_t var_hash_tbl[VAR_HASH_SIZE];
static bool    var_hash_valid = false;

static inline uint32_t var_hash(const char* n) {
    uint32_t h = 2166136261u; // FNV-1a（先頭 8 文字、null で打ち切り）
    for (int i = 0; i < 8 && n[i]; i++) { h ^= (uint8_t)n[i]; h *= 16777619u; }
    return h;
}

static void var_hash_put(int slot) {
    uint16_t addr = MEMORY_VAR_BASE + (slot * 16);
    uint32_t h = var_hash((const char*)&logical_memory[addr]) & (VAR_HASH_SIZE - 1);
    while (var_hash_tbl[h] >= 0) h = (h + 1) & (VAR_HASH_SIZE - 1);
    var_hash_tbl[h] = (int16_t)slot;
}

static void build_var_hash() {
    for (int i = 0; i < VAR_HASH_SIZE; i++) var_hash_tbl[i] = -1;
    for (int i = 0; i < MAX_VARIABLES; i++) {
        uint16_t addr = MEMORY_VAR_BASE + (i * 16);
        if (logical_memory[addr + 9] != 0) var_hash_put(i);
    }
    var_hash_valid = true;
}

void invalidate_var_hash() { var_hash_valid = false; }

// スロットのアドレスを返す。無ければ 0xFFFF
static uint16_t find_var_slot(const char* name) {
    if (!var_hash_valid) build_var_hash();
    uint32_t h = var_hash(name) & (VAR_HASH_SIZE - 1);
    for (int probe = 0; probe < VAR_HASH_SIZE; probe++) {
        int slot = var_hash_tbl[h];
        if (slot < 0) return 0xFFFF; // 空き → 存在しない
        uint16_t addr = MEMORY_VAR_BASE + (slot * 16);
        if (logical_memory[addr + 9] != 0 &&
            strncmp((const char*)&logical_memory[addr], name, 8) == 0) return addr;
        h = (h + 1) & (VAR_HASH_SIZE - 1);
    }
    return 0xFFFF;
}

bool get_variable(const char* name, Value& out_val) {
    uint16_t addr = find_var_slot(name);
    if (addr == 0xFFFF) return false;
    out_val = read_compact_value(addr);
    return true;
}

void set_variable(const char* name, const Value& val) {
    uint16_t addr = find_var_slot(name);
    if (addr != 0xFFFF) {
        write_compact_value(addr, val);
        return;
    }
    // 新規変数: 空きスロットに置き、ハッシュにも登録する
    for (int i = 0; i < MAX_VARIABLES; ++i) {
        uint16_t saddr = MEMORY_VAR_BASE + (i * 16);
        if (logical_memory[saddr + 9] == 0) {
            logical_memory[saddr + 9] = 1;
            strncpy((char*)&logical_memory[saddr], name, 8);
            write_compact_value(saddr, val);
            if (var_hash_valid) var_hash_put(i);
            return;
        }
    }
    throw std::runtime_error("Out of Memory: Too many variables");
}

int flatten_array_index(const ArrayRef* arr, int i, int j) {
    if (j < 0) {
        if (arr->ndim == 2)
            throw std::runtime_error("Syntax Error: 2D array requires 2 indices A(row,col)");
        if (i < 0 || i >= (int)arr->dim1)
            throw std::runtime_error("Array index out of bounds");
        return i;
    } else {
        if (arr->ndim != 2)
            throw std::runtime_error("Syntax Error: 1D array requires 1 index A(idx)");
        if (i < 0 || i >= (int)arr->dim1 || j < 0 || j >= (int)arr->dim2)
            throw std::runtime_error("Array index out of bounds");
        return i * (int)arr->dim2 + j;
    }
}

void write_heap_value(uint16_t addr, const Value& val) {
    if (val.type == Value::Type::STR) {
        uint8_t old_type = logical_memory[addr];
        if ((Value::Type)old_type == Value::Type::STR) {
            uint16_t old_str_ptr;
            memcpy(&old_str_ptr, &logical_memory[addr + 4], 2);
            if (old_str_ptr >= STRING_HEAP_BASE && old_str_ptr < string_heap_ptr) {
                uint16_t old_alloc;
                memcpy(&old_alloc, &logical_memory[old_str_ptr], 2);
                int new_len = (int)strlen(val.str_val);
                if (old_alloc > 0 && (uint16_t)(new_len + 1) <= old_alloc) {
                    memcpy(&logical_memory[old_str_ptr + 2], val.str_val, (size_t)(new_len + 1));
                    logical_memory[addr] = (uint8_t)val.type;
                    return;
                }
            }
        }
        uint16_t str_ptr = string_heap_ptr;
        int len = (int)strlen(val.str_val);
        uint16_t alloc_size = (uint16_t)(len + 1);
        if ((uint32_t)str_ptr + 2 + alloc_size >= 65536u)
            throw std::runtime_error("String Heap Overflow");
        logical_memory[str_ptr]     = (uint8_t)(alloc_size & 0xFF);
        logical_memory[str_ptr + 1] = (uint8_t)(alloc_size >> 8);
        memcpy(&logical_memory[str_ptr + 2], val.str_val, (size_t)alloc_size);
        memcpy(&logical_memory[addr + 4], &str_ptr, 2);
        string_heap_ptr = (uint16_t)(str_ptr + 2 + alloc_size);
        logical_memory[addr] = (uint8_t)val.type;
        return;
    }
    logical_memory[addr] = (uint8_t)val.type;
    if (val.type == Value::Type::NUM) {
        memcpy(&logical_memory[addr + 4], &val.num_val, 4);
    } else if (val.type == Value::Type::INT) {
        memcpy(&logical_memory[addr + 4], &val.int_val, 4);
    }
}

Value read_heap_value(uint16_t addr) {
    Value v;
    v.type = (Value::Type)logical_memory[addr];
    if (v.type == Value::Type::NUM) {
        memcpy(&v.num_val, &logical_memory[addr + 4], 4);
    } else if (v.type == Value::Type::INT) {
        memcpy(&v.int_val, &logical_memory[addr + 4], 4);
        v.num_val = (float)v.int_val;
    } else if (v.type == Value::Type::STR) {
        uint16_t str_ptr;
        memcpy(&str_ptr, &logical_memory[addr + 4], 2);
        strncpy(v.str_val, (const char*)&logical_memory[str_ptr + 2], 127);
        v.str_val[127] = '\0';
    }
    return v;
}

ArrayRef* get_array(const char* name) {
    static ArrayRef temp_arr;
    for (int i = 0; i < MAX_VARIABLES; ++i) {
        uint16_t addr = ARRAY_TABLE_BASE + (i * 16);
        bool active = logical_memory[addr + 8] != 0;
        if (active && strncmp((const char*)&logical_memory[addr], name, 8) == 0) {
            strncpy(temp_arr.name, (const char*)&logical_memory[addr], 8);
            temp_arr.name[8] = '\0';
            temp_arr.active  = true;
            temp_arr.ndim    = logical_memory[addr + 9];
            memcpy(&temp_arr.dim1,       &logical_memory[addr + 10], 2);
            memcpy(&temp_arr.dim2,       &logical_memory[addr + 12], 2);
            memcpy(&temp_arr.start_addr, &logical_memory[addr + 14], 2);
            return &temp_arr;
        }
    }
    return nullptr;
}
