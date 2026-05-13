#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define READ_BUF_SIZE 256
#define DA_INIT_CAP 128

#define DA_APPEND(arr, item)\
    do {\
        if ((arr)->size+1 > (arr)->capacity) {\
            uint32_t new_capacity = (arr)->capacity * 2;\
            if (new_capacity == 0) {\
                new_capacity = DA_INIT_CAP;\
            }\
            (arr)->items = realloc((arr)->items, sizeof((arr)->items[0]) * new_capacity);\
            assert((arr)->items != NULL);\
            (arr)->capacity = new_capacity;\
        }\
        (arr)->items[(arr)->size++] = (item);\
    } while(0)

typedef enum {
	RIGHT            = '>',
	LEFT             = '<',
	INC              = '+',
	DEC              = '-',
	OUT              = '.',
	IN               = ',',
    JUMP_IF_ZERO     = '[',
	JUMP_IF_NOT_ZERO = ']'
} Op_kind;

typedef struct {
    uin32_t fileRow;
    uint32_t fileColumn;
	Op_kind kind;
 
    // If op is a jump kind, value is the index of the other pairing jump in the Ops array.
    // Else it is just the count.
	uint32_t value;
} Op;

typedef struct {
	Op *items;
	uint32_t size;
	uint32_t capacity;
} Ops;

typedef struct {
    uint8_t *items;
    uint32_t capacity;
} Tape;

Tape new_tape() {
    Tape tape = {0};
    tape.items = malloc(sizeof(uint8_t) * DA_INIT_CAP);
    tape.capacity = DA_INIT_CAP;
    return tape;
}

void tape_try_grow(Tape *tape, uint32_t dataPointer) {
    while (dataPointer >= tape->capacity) {
        tape->capacity *= 2;
        tape->items = realloc(tape->items, sizeof(uint8_t) * tape->capacity);
    }
}

typedef struct {
    uint8_t *items;
    uint32_t size;
    uint32_t capacity;
} Code;

void emit_byte(Code *code, uint8_t data) {
    DA_APPEND(code, data);
}

void emit_bytes(Code *code, int count, ...) {
    va_list ap;
    va_start(ap, count);
    while (count > 0) {
        DA_APPEND(code, va_arg(ap, int));
        count--;
    }
    va_end(ap);
}

void emit_bytes_arr(Code *code, int size, uint8_t *items) {
    for (int i = 0; i < size; i++) {
        DA_APPEND(code, items[i]);
    }
}

void emit_num(Code *code, uint8_t byteCount, uint64_t x) {
    uint8_t *arr = (uint8_t*) &x;
    for (int i = 0; i < byteCount; i++) {
        emit_byte(code, arr[i]);
    }
}

typedef struct {
    Op *op;
    uint32_t codeIndex;
} Marker;

typedef struct {
    Marker *items;
    uint32_t size;
    uint32_t capacity;
} MarkerStack;

void push_marker(MarkerStack *stack, Code *code, Op *op) {
    DA_APPEND(stack, {op, .codeIndex = code->size-1});
}

CodeMarker pop_marker(CodeMarkerStack *stack) {
    if (stack.size == 0) {
        assert(0 && "Empty code marker stack");
    }
    return stack->items[--stack->size];
} 

void jit(Ops *ops) {
    Tape tape = new_tape();
    Code code = {0};
    CodeMarkerStack markerStack = {0};

    // r15 = Tape Pointer
    // r14 = Data Index

    // set r15 to &tape
    emit_bytes(&code, 2, 0x49, 0xbf); // movabs r15
    emit_num(&code, 8, (uint64_t) &tape);

    // Set r14 to 0
    emit_bytes(&code, 3, 0x4d, 0x31, 0xf6); // xor r14, r14

    for (int i = 0; i < ops->size; i++) {
        Op op = ops->items[i];
        switch (op.kind) {
            case RIGHT: {
                // Add op.value to r14
                emit_bytes(&code, 3, 0x49, 0x81, 0xc6); // add r14
                emit_num(&code, 4, op.value);

                emit_bytes(&code, 3, 0x4c, 0x89, 0xf7); // mov rdi, r14

                emit_bytes(&code, 2, 0x48, 0xb8); // movabs rax
                emit_num_uint64(&code, (uint64_t) &tape_try_grow); // tryGrowTape func ptr
                emit_bytes(&code, 2, 0xff, 0xd0); // call tryGrowTape
            } break;
            case LEFT: {
                // if dp - op.value < 0  then error
                // if 0 >= dp - op.value then continue
                // if op.value > dp      then error
                // dont error if dp <= op.value
                // jmp if

                // Compare op.value (immediate) and r14 (dataPointer.)
                // If the this op will result in going under 0, throw error ()
                // else we operate 
                if (op.value > DP) {
                    assert(0 && "Cannot move data pointer below 0");
                }
                DP -= op.value;
            } break;
            case INC: 
            case DEC: {
                uint32_t i = op.value;
                while (i > 0) {
                    uint8_t x = i < 256 ? (uint8_t) i : 255;
                    i -= x;
                    if (op.kind == INC) {
                        emit_bytes(&code, 5, 0x43, 0x80, 0x04, 0x37, x); // add byte [r15+r14], immediate x
                    } else {
                        emit_bytes(&code, 5, 0x43, 0x80, 0x2c, 0x37, x); // sub byte [r15+r14], immediate x
                    }
                }
            } break;
            case OUT: {
                // if op.value == 1 then a = [r15+r14] else a = [r15+r14]*op.value length
                // write(1, a, op.value)
                for (int i = 0; i < op.value; i++) {
                    putchar(tape.items[DP]);
                }
            } break;
            case IN: {
                for (int i = 0; i < op.value; i++) {
                    tape.items[DP] = getchar();
                }
            } break;
            case JUMP_IF_ZERO: { // [
            } break;
            case JUMP_IF_NOT_ZERO { // ]
            } break;
            default: {
                fprintf(stderr, "Unknown OP kind %d, %d, %ld", op.kind, op.value, op.readerIndex);
                exit(1);
            }
        }
    }

    printf("s: %d\n", code.size);
    for (int i = 0; i < code.size; i++) {
        printf("%08x\n", code.items[i]);
    }

    void *exeCode = mmap(NULL, code.size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (exeCode == MAP_FAILED) {
        perror("Error mmap");
        exit(1);
    }
    memcpy(exeCode, code.items, code.size);
    free(code.items);
    mprotect(exeCode, code.size, PROT_EXEC);
    int result = ((int(*)())exeCode)();
    printf("return: %d\n", result);
    munmap(exeCode, code.size);
}

void interpret(Ops *ops) {
    Tape tape = new_tape();
    uint32_t DP = 0;
    uint32_t IP = 0;
    printf("Interpretting...\n");
    while (IP < ops->size) {
        Op op = ops->items[IP];
        if ((op.kind == JUMP_IF_ZERO && tape.items[DP] == 0) || (op.kind == JUMP_IF_NOT_ZERO && tape.items[DP] != 0)) {
            IP = op.value;
        } else {
            switch(op.kind) {
                case RIGHT: {
                    DP += op.value;
                    tape_try_grow(&tape, DP);
                } break;
                case LEFT: {
                    if (op.value > DP) {
                        assert(0 && "Cannot move data pointer below 0");
                    }
                    DP -= op.value;
                } break;
                case INC: {
                    tape.items[DP] += op.value;
                } break;
                case DEC: {
                    tape.items[DP] -= op.value;
                } break;
                case OUT: {
                    for (int i = 0; i < op.value; i++) {
                        putchar(tape.items[DP]);
                    }
                } break;
                case IN: {
                    for (int i = 0; i < op.value; i++) {
                        tape.items[DP] = getchar();
                    }
                } break;
                default: {
                    if (op.kind != JUMP_IF_ZERO && op.kind != JUMP_IF_NOT_ZERO) {
                        fprintf(stderr, "Unknown OP kind %d, %d, %ld", op.kind, op.value, op.fileIndex);
                        exit(1);
                    }
                }
            }
            IP++;
        }
    }
    printf("\n");
}

typedef struct {
    uint32_t* items;
    uint32_t size;
    uint32_t capacity;
} LexerJumpStack;

int main(int argc, char **argv) {
	assert(argc > 0);
	if (argc != 3) {
		printf("Usage: %s <input.bf> <jit/interpret>\n", argv[0]);
		return 1; 
	}

	char *path = argv[1];
    char *executionType = argv[2];

	int file = open(path, 0);
	size_t file_size = lseek(file, 0, SEEK_END);
	lseek(file, 0, SEEK_SET);

    Ops ops = {0};
    LexerJumpStack jumpStack = {0};

	char buf[READ_BUF_SIZE];
	size_t buf_size;
    uint32_t fileRow = 0;
    uint32_t fileColumn = 0;
	while ((buf_size = read(file, buf, READ_BUF_SIZE)) > 0) {
        for (int i = 0; i < buf_size; i++) {
            switch (buf[i]) {
                case RIGHT:
                case LEFT:
                case INC:
                case DEC:
                case OUT:
                case IN: {
                    Op op = {.kind = buf[i], .value = 1, fileRow, fileColumn};
                    while (i+1 < buf_size && op.kind == buf[i+1]) {
                        op.value++;
                        i++;
                        fileColumn++;
                    } 
                    DA_APPEND(&ops, op);
                } break;
                case '[': {
                    Op op = {.kind = '[', fileRow, fileColumn};
                    DA_APPEND(&jumpStack, ops.size);
                    DA_APPEND(&ops, op);
                } break;
                case ']': {
                    if (jumpStack.size == 0) {
                        fprintf(stderr, "Error while parsing file. Found closing loop with no opening loop pair\n");
                        fprintf(stderr, "fileRow: %d, fileColumn: %d\n", fileRow, fileColumn);
                        return 1;
                    }
                    uint32_t leftIndex = jumpStack.items[--jumpStack.size];
                    Op right = {.kind = ']', .value = leftIndex+1, fileRow, fileColumn };
                    DA_APPEND(&ops, right);
                    ops.items[leftIndex].value = ops.size;
                } break;
            }
            if (buf[i] == "\n) {
                fileRow++;
                fileColumn = 0;
            } else {
                fileColumn++;
            }
        }
    }
    free(jumpStack.items);
    if (buf_size < 0) {
        perror("Failed to read file");
        return 1;
    }

    if (executionType == "jit") {
        jit(&ops);
    } else if (executionType == "interpret") {
        interpret(&ops);
    } else {
        fprintf(stderr, "Unknown execution type \"%s\"", executionType);
        return 1;
    }

    free(ops.items);
    return 0;
}
