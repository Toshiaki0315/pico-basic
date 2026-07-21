#include "parser.h"
#include "parser_internal.h"
#include "hal_display.h"
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------
// Interpreter State
// ---------------------------------------------------------
float last_rnd_val = 0.5f;

int current_line = -1;
bool branch_taken = false;

uint16_t current_color_565 = 0xFFFF; // Default White
const uint16_t PALETTE[16] = {
    0x0000, // 0: Black
    0x001F, // 1: Blue
    0x07E0, // 2: Green
    0x07FF, // 3: Cyan
    0xF800, // 4: Red
    0xF81F, // 5: Magenta
    0xFBE0, // 6: Brown
    0xC618, // 7: Light Gray
    0x7BEF, // 8: Dark Gray
    0x55FF, // 9: Light Blue
    0x5FE0, // 10: Light Green
    0x5FFF, // 11: Light Cyan
    0xFF80, // 12: Light Red
    0xFFDF, // 13: Light Magenta
    0xFFE0, // 14: Yellow
    0xFFFF  // 15: White
};

ForLoopContext for_stack[MAX_FOR_STACK];
int for_stack_ptr = 0;

int call_stack[MAX_CALL_STACK];
int call_stack_ptr = 0;

Value data_buffer[MAX_DATA_BUFFER];
int data_buffer_size = 0;
int data_ptr = 0;

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------
bool parse_and_execute(const TokenList& tokens) {
    if (tokens.size == 0 || tokens.tokens[0].type == TokenType::END_OF_FILE) return false;
    
    try {
        if (tokens.tokens[0].type == TokenType::NUMBER) {
            int line_num = atoi(tokens.tokens[0].text);
            TokenList remainder;
            int j = 0;
            for (int i = 1; i < tokens.size; i++) remainder.tokens[j++] = tokens.tokens[i];
            remainder.size = j;
            store_line(line_num, remainder);
            return true;
        } else if (tokens.tokens[0].type == TokenType::NEW) {
            clear_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::LIST) {
            list_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::RUN) {
            run_program();
            return false;
        } else if (tokens.tokens[0].type == TokenType::SAVE) {
            int p = 0; execute_save(tokens, p);
            return false;
        } else if (tokens.tokens[0].type == TokenType::LOAD) {
            int p = 0; execute_load(tokens, p);
            return false;
        } else if (tokens.tokens[0].type == TokenType::FILES) {
            int p = 0; execute_files(tokens, p);
            return false;
        }
        int pos = 0;
        branch_taken = false;
        while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
            execute_statement(tokens, pos);
            if (branch_taken) break;
            if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COLON) {
                pos++;
            } else break;
        }
    } catch (const std::exception& e) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s\n", e.what());
        basic_print(buf);
    }
    return false;
}

void run_program(int max_steps) {
    for_stack_ptr = 0;
    call_stack_ptr = 0;
    
    // Pre-scan DATA statements
    data_buffer_size = 0;
    data_ptr = 0;
    
    uint16_t ptr = MEMORY_TEXT_BASE;
    while (true) {
        if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) break;
        TokenList tokens = get_detokenized_line(ptr);
        
        if (tokens.size > 0 && tokens.tokens[0].type == TokenType::DATA) {
            int pos = 1;
            while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
                if (data_buffer_size < MAX_DATA_BUFFER) {
                    data_buffer[data_buffer_size++] = parse_relation(tokens, pos);
                }
                if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COMMA) pos++;
                else if (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) break;
            }
        }
        uint16_t next_ptr = logical_memory[ptr] | (logical_memory[ptr+1] << 8);
        if (next_ptr == 0) break;
        ptr = next_ptr;
    }
    
    // Interpreter loop
    ptr = MEMORY_TEXT_BASE;
    if (logical_memory[ptr+2] == 0 && logical_memory[ptr+3] == 0 && ptr != MEMORY_TEXT_BASE) return;
    current_line = logical_memory[ptr+2] | (logical_memory[ptr+3] << 8);
    int steps = 0;

    while (current_line != -1 && (max_steps == -1 || steps < max_steps)) {
        uint16_t line_ptr = find_program_line(current_line);
        if (line_ptr == 0xFFFF) break;
        
        TokenList tokens = get_detokenized_line(line_ptr);
        int pos = 0;
        branch_taken = false;
        
        try {
            while (pos < tokens.size && tokens.tokens[pos].type != TokenType::END_OF_FILE) {
                execute_statement(tokens, pos);
                if (branch_taken) break;
                if (pos < tokens.size && tokens.tokens[pos].type == TokenType::COLON) {
                    pos++;
                } else break;
            }
        } catch (const std::exception& e) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Error in line %d: %s\n", current_line, e.what());
            basic_print(buf);
            break;
        }
        
        if (!branch_taken) {
            uint16_t next_ptr = logical_memory[line_ptr] | (logical_memory[line_ptr+1] << 8);
            if (next_ptr == 0 || (logical_memory[next_ptr+2] == 0 && logical_memory[next_ptr+3] == 0)) {
                current_line = -1;
            } else {
                current_line = logical_memory[next_ptr+2] | (logical_memory[next_ptr+3] << 8);
            }
        }
        steps++;
    }
}
