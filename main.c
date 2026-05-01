#include <assert.h>
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
#define DA_INIT_CAP 8

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
	Op_kind kind;
    // for JUMP op kind, value is the other pairing jump op index in Ops array.
    // Otherwise its just the count.
	uint32_t value;
    size_t readerIndex;
} Op;

typedef struct {
	Op *items;
	uint32_t size;
	uint32_t capacity;
} Ops;

typedef struct {
    uint32_t* items;
    uint32_t size;
    uint32_t capacity;
} JumpStack;

typedef struct {
    uint8_t *items;
    uint32_t capacity;
} Tape;

typedef struct {
    uint8_t *items;
    uint32_t size;
    uint32_t capacity;
} Code;

void DA_APPEND_CODE(Code *code, int args, ...) {
    va_list ap;
    va_start(ap, args);
    while (args > 0) {
        DA_APPEND(code, va_arg(ap, int));
        args--;
    }
    va_end(ap);
}

void rexPrefix(Code *code, bool w) {
    uint8_t a = 0b01000000;
    if (w) {
        a |= 0b1000;
    }
    DA_APPEND_CODE(code, 1, a)
}

void _jitTryGrowTape(Tape *tape, uint64_t dataPointer) {
    while (dataPointer >= tape->capacity) {
        tape->capacity *= 2;
        tape->items = realloc(tape->items, sizeof(uint8_t) * tape->capacity);
        if (tape->items == null) {
            fprintf(stderr, "Failed to grow tape");
            exit(1);
        }
    }
}

void jit(Ops *ops) {
    Code code = {0};

    Tape tape = {0};
    tape.items = malloc(sizeof(uint8_t) * DA_INIT_CAP);
    tape.capacity = DA_INIT_CAP;

    #define CODE(size, args...) DA_APPEND_CODE(&code, size, args)
    #define REX_PREFIX(W, R, X, B)

    // r15 = DP

    for (int i = 0; i < ops->size; i++) {
        Op *op = ops->items[i];
        switch (op.kind) {
            case RIGHT: {
                // ADD op.value to r15
                CODE(1);
                DP += op.value;
                while (DP >= tape.capacity) {
                    tape.capacity *= 2;
                    tape.items = realloc(tape.items, sizeof(uint8_t) * tape.capacity);
                }
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
                    fprintf(stderr, "Unknown OP kind %d, %d, %ld", op.kind, op.value, op.readerIndex);
                    exit(1);
                }
            }
        }
    }

    // mov eax, 69
    CODE(5, 0xB8, 69, 0, 0, 0);
    // ret
    CODE(1, 0xC3);

    #undef CODE

    printf("s: %d\n", code.size);
    for (int i = 0; i < code.size; i++) {
        printf("%08x\n", code.items[i]);
    }

    void *exeCode = mmap(NULL, code.size, PROT_EXEC | PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (exeCode == MAP_FAILED) {
        perror("Error mmap");
        exit(1);
    }

    memcpy(exeCode, code.items, code.size);
    free(code.items);
    int result = ((int(*)())exeCode)();
    printf("return: %d\n", result);
    munmap(exeCode, code.size);
}

void interpret(Ops *ops) {
    uint32_t DP = 0;
    uint32_t IP = 0;
    Tape tape = {0};
    tape.items = malloc(sizeof(uint8_t) * DA_INIT_CAP);
    tape.capacity = DA_INIT_CAP;
    printf("Interpretting...\n");
    while (IP < ops->size) {
        Op op = ops->items[IP];
        if ((op.kind == JUMP_IF_ZERO && tape.items[DP] == 0) || (op.kind == JUMP_IF_NOT_ZERO && tape.items[DP] != 0)) {
            IP = op.value;
        } else {
            switch(op.kind) {
                case RIGHT: {
                    DP += op.value;
                    while (DP >= tape.capacity) {
                        tape.capacity *= 2;
                        tape.items = realloc(tape.items, sizeof(uint8_t) * tape.capacity);
                    }
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
                        fprintf(stderr, "Unknown OP kind %d, %d, %ld", op.kind, op.value, op.readerIndex);
                        exit(1);
                    }
                }
            }
            IP++;
        }
    }
    printf("\n");
}

int main(int argc, char **argv) {
	assert(argc > 0);
	if (argc != 2) {
		printf("Usage: %s <input.bf>\n", argv[0]);
		return 1; 
	}

	char *path = argv[1];
	int file = open(path, 0);
	size_t file_size = lseek(file, 0, SEEK_END);
	lseek(file, 0, SEEK_SET);

    Ops ops = {0};
    JumpStack jumpStack = {0};

	char buf[READ_BUF_SIZE];
	size_t buf_size;
    size_t readerIndex = 0;
	while ((buf_size = read(file, buf, READ_BUF_SIZE)) > 0) {
		for (int i = 0; i < buf_size; i++) {
            switch (buf[i]) {
                case '>': 
                case '<':
                case '+':
                case '-':
                case '.':
                case ',': {
                    Op op = {.kind = buf[i], .value = 1, readerIndex};
                    while (i+1 < buf_size && op.kind == buf[i+1]) {
                        op.value++;
                        i++;
                        readerIndex++;
                    } 
                    DA_APPEND(&ops, op);
                } break;
                case '[': {
                    Op op = {.kind = '[', readerIndex};
                    DA_APPEND(&jumpStack, ops.size);
                    DA_APPEND(&ops, op);
                } break;
                case ']': {
                    if (jumpStack.size == 0) {
                        fprintf(stderr, "Error while parsing file. Found closing loop with no opening loop pair\n");
                        fprintf(stderr, "readerIndex of unpaired closing loop: %ld\n", readerIndex);
                        return 1;
                    }
                    uint32_t leftIndex = jumpStack.items[--jumpStack.size];
                    Op right = {.kind = ']', .value = leftIndex+1, readerIndex };
                    DA_APPEND(&ops, right);
                    ops.items[leftIndex].value = ops.size;
                } break;
                default: {}
			}
            readerIndex++;
		}
	}
	if (buf_size < 0) {
		perror("Failed to read file");
		return 1;
	}

    free(jumpStack.items);
    
//    printf("%d,%d\n", ops.size, ops.capacity);
//    for (int i = 0; i < ops.size; i++) {
//        printf("index=%d, enum_int=%d, enum_char=%c, value=%c/%d\n", i, ops.items[i].kind, ops.items[i].kind, ops.items[i].value, ops.items[i].value);
//    }

    jit(&ops);
    //interpret(&ops);
    free(ops.items);
    return 0;
}
