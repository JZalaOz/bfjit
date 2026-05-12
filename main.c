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
    uint8_t *items;
    uint32_t capacity;
} Tape;

typedef struct {
    uint8_t *items;
    uint32_t size;
    uint32_t capacity;
} Code;

void emit_byte(Code *code, uint8_t data) {
    DA_APPEND(code, data);
}

void emit_bytes(Code *code, int args, ...) {
    va_list ap;
    va_start(ap, args);
    while (args > 0) {
        DA_APPEND(code, va_arg(ap, int));
        args--;
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
    uint32_t codeIndex;
    uint32_t opIndex;
} CodeMarker;

typedef struct {
    CodeMarker *items;
    uint32_t size;
    uint32_t capacity;
} CodeMarkerStack;

void push_marker(CodeMarkerStack *stack, Code *code, uint32_t opIndex) {
    DA_APPEND(stack, {.codeIndex = code->size-1, opIndex});
}

CodeMarker pop_marker(CodeMarkerStack *stack) {
    if (stack.size == 0) {
        assert(0 && "Empty code marker stack");
    }
    return stack->items[--stack->size];
} 

void tryGrowTape(Tape *tape, uint64_t dataPointer) {
    while (dataPointer >= tape->capacity) {
        tape->capacity *= 2;
        tape->items = realloc(tape->items, sizeof(uint8_t) * tape->capacity);
    }
}

    /*emit_bytes(&code, 2, 0x41, 0xbe); // mov r14
    emit_num_uint32(&code, 69420); // 4 byte little endian data

    // mov eax r14d
    emit_bytes(&code, 3, 0x44, 0x89, 0xf0);

    // mov rax, tryGrowTape ptr
    emit_bytes(&code, 2, 0x48, 0xb8);
    emit_num_uint64(&code, (uint64_t) &tryGrowTape);

    // call
    emit_bytes(&code, 2, 0xff, 0xd0);

    // ret
    emit_byte(&code, 0xc3);//*/

void jit(Ops *ops) {
    Tape tape = {0};
    tape.items = malloc(sizeof(uint8_t) * DA_INIT_CAP);
    tape.capacity = DA_INIT_CAP;

    Code code = {0};
    CodeMarkerStack markerStack = {0};

    // r15 = Tape Pointer
    // r14 = Data Index

    // Set r14 to 0
    emit_bytes(&code, 3, 0x4d, 0x31, 0xf6); // xor r14, r14

    // Move &tape into r15
    emit_bytes(&code, 2, 0x49, 0xbf); // movabs r15
    emit_num(&code, 8, (uint64_t) &tape);

    for (int i = 0; i < ops->size; i++) {
        Op op = ops->items[i];
        switch (op.kind) {
            case RIGHT: {
                // Add op.value to r14
                emit_bytes(&code, 3, 0x49, 0x81, 0xc6); // add r14
                emit_num(&code, 4, op.value);

                emit_bytes(&code, 3, 0x4c, 0x89, 0xf7); // mov rdi, r14

                emit_bytes(&code, 2, 0x48, 0xb8); // movabs rax
                emit_num_uint64(&code, (uint64_t) &tryGrowTape); // tryGrowTape func ptr
                emit_bytes(&code, 2, 0xff, 0xd0); // call tryGrowTape
            } break;
            case LEFT: {
                // if dp - op.value < 0  then error
                // if 0 >= dp - op.value then continue
                // if op.value > dp      then error
                // dont error if dp <= op.value
                // jmp if

                if (op.value > DP) {
                    assert(0 && "Cannot move data pointer below 0");
                }
                DP -= op.value;
            } break;
            case INC: {
                uint32_t i = op.value;
                while (i > 0) {
                    uint32_t x;
                    if (i < 256) {
                        x = i;
                    } else {
                        x = 255;
                        i -= x;
                    }
                    emit_bytes(&code, 5, 0x43, 0x80, 0x04, 0x37, i); // add byte [r15+r14], 1
                }
                if (op.value == 1) {
                    emit_bytes(&code, 5, 0x43, 0x80, 0x04, 0x37, 0x01); // add byte [r15+r14], 1
                } else {
                    whil
                    emit_bytes(&code, 
                }
            } break;
            case DEC: {
                emit_bytes(&code, 5, 0x43, 0x80, 0x2c, 0x37, 0x01); // sub byte [r15+r14], 1
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

typedef struct {
    uint32_t* items;
    uint32_t size;
    uint32_t capacity;
} LexerLoopStack;

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
    LexerLoopStack loopStack = {0};

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
                    DA_APPEND(&loopStack, ops.size);
                    DA_APPEND(&ops, op);
                } break;
                case ']': {
                    if (loopStack.size == 0) {
                        fprintf(stderr, "Error while parsing file. Found closing loop with no opening loop pair\n");
                        fprintf(stderr, "readerIndex of unpaired closing loop: %ld\n", readerIndex);
                        return 1;
                    }
                    uint32_t leftIndex = loopStack.items[--jumpStack.size];
                    Op right = {.kind = ']', .value = leftIndex+1, readerIndex };
                    DA_APPEND(&ops, right);
                    ops.items[leftIndex].value = ops.size;
                } break;
            }
            readerIndex++;
        }
    }
    free(loopStack.items);
    if (buf_size < 0) {
        perror("Failed to read file");
        return 1;
    }

    jit(&ops);
    //interpret(&ops);
    free(ops.items);
    return 0;
}
