CC      = gcc
CFLAGS  = -Wall -Werror -Wextra
LDFLAGS = -lreadline -lpthread

# Sources
SRCS_LIB = gescom.c creme.c
OBJS_LIB = $(SRCS_LIB:.c=.o)
OBJS_MAIN = biceps.o

TARGET        = biceps
TARGET_MEMCHK = biceps-memory-leaks

# ─── Cible par défaut ─────────────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJS_MAIN) $(OBJS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ─── Build pour valgrind ──────────────────────────────────────────────────
memory-leak: CFLAGS += -g -O0
memory-leak: $(OBJS_LIB)
	$(CC) $(CFLAGS) -g -O0 -c biceps.c -o biceps_ml.o
	$(CC) $(CFLAGS) -g -O0 -o $(TARGET_MEMCHK) biceps_ml.o $(OBJS_LIB) $(LDFLAGS)
	rm -f biceps_ml.o

# ─── Compilation avec traces ──────────────────────────────────────────────
trace: CFLAGS += -DTRACE
trace: $(TARGET)

# ─── Règle générique .c → .o ──────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Dépendances ──────────────────────────────────────────────────────────
biceps.o: biceps.c gescom.h creme.h
gescom.o: gescom.c gescom.h creme.h
creme.o:  creme.c  creme.h

# ─── Nettoyage ────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(TARGET_MEMCHK) *.o

.PHONY: all clean trace memory-leak
