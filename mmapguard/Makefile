# Makefile de mmapguard.
#
# Objetivos: all (= bpf + user), bpf, user, test, clean, help.
#
# Toolchain de usuario (sin root): TC_PREFIX (por defecto ~/.mmapguard-tc) con clang, bpftool
# y las cabeceras de libbpf. Si no existe se usan clang y bpftool del PATH. Si falta alguno,
# `make bpf` y `make user` avisan en una linea y terminan con exito (exit 0): solo se
# construyen policy.o y events.o con gcc, que no necesitan libbpf ni toolchain BPF.
#
# Se compila en una copia fuera del repo (ver CONTRATO.md); todo lo generado va a build/.

TC_PREFIX ?= $(HOME)/.mmapguard-tc

# Valores por defecto: toolchain de usuario si existe; si no, el del PATH.
CLANG   ?= $(if $(wildcard $(TC_PREFIX)/usr/lib/llvm-18/bin/clang),$(TC_PREFIX)/usr/lib/llvm-18/bin/clang,clang)
BPFTOOL ?= $(if $(wildcard $(TC_PREFIX)/usr/lib/linux-tools-*/bpftool),$(firstword $(wildcard $(TC_PREFIX)/usr/lib/linux-tools-*/bpftool)),bpftool)

# Bibliotecas del toolchain de usuario para clang y bpftool.
export LD_LIBRARY_PATH := $(TC_PREFIX)/usr/lib/x86_64-linux-gnu:$(TC_PREFIX)/usr/lib/llvm-18/lib$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))

CC := gcc

BUILD := build

CFLAGS_USER := -Wall -Wextra -Werror -std=gnu11 -Iinclude -Isrc
CFLAGS_BPF  := -target bpf -O2 -g -Wall -D__TARGET_ARCH_x86 -I$(BUILD) -Iinclude -I$(TC_PREFIX)/usr/include

VMLINUX  := $(BUILD)/vmlinux.h
BPF_OBJ  := $(BUILD)/mmapguard.bpf.o
BPF_LINK := $(BUILD)/mmapguard.o
SKEL     := $(BUILD)/mmapguard.skel.h
BIN      := $(BUILD)/mmapguard
POLICY_O := $(BUILD)/policy.o
EVENTS_O := $(BUILD)/events.o

HDRS := $(wildcard include/*.h src/*.h)

comma := ,
empty :=
space := $(empty) $(empty)

# Deteccion de la toolchain BPF (al interpretar el Makefile).
HAVE_CLANG   := $(shell command -v '$(CLANG)' 2>/dev/null)
HAVE_BPFTOOL := $(shell command -v '$(BPFTOOL)' 2>/dev/null)
HAVE_BTF     := $(wildcard /sys/kernel/btf/vmlinux)
HAVE_LIBBPF  := $(or $(wildcard $(TC_PREFIX)/usr/include/bpf/libbpf.h),$(wildcard /usr/include/bpf/libbpf.h))

MISSING := $(if $(HAVE_CLANG),,clang[CLANG=$(CLANG)]) $(if $(HAVE_BPFTOOL),,bpftool[BPFTOOL=$(BPFTOOL)]) $(if $(HAVE_BTF),,BTF[/sys/kernel/btf/vmlinux])
MISSING := $(subst $(space),$(comma)$(space),$(strip $(MISSING)))
ifeq ($(MISSING),)
CAN_BPF := 1
else
CAN_BPF :=
endif

# El binario solo se construye si hay (o se puede generar) el skeleton y estan las cabeceras de libbpf.
ifneq ($(and $(HAVE_LIBBPF),$(or $(CAN_BPF),$(wildcard $(SKEL)))),)
USER_TARGETS := $(POLICY_O) $(EVENTS_O) $(BIN)
else
USER_TARGETS := $(POLICY_O) $(EVENTS_O)
endif

.PHONY: all bpf user test clean help bench
.DELETE_ON_ERROR:

all: bpf user

ifeq ($(CAN_BPF),1)
bpf: $(SKEL)
else
bpf:
	@echo "aviso: falta $(MISSING): se omite el objeto BPF y el skeleton; instale clang y bpftool o fije TC_PREFIX=<ruta del toolchain>. make termina con exito (exit 0) por ausencia de toolchain."
endif

user: $(USER_TARGETS)
ifeq ($(findstring $(BIN),$(USER_TARGETS)),)
	@echo "aviso: no se construye $(BIN) porque falta build/mmapguard.skel.h (toolchain BPF ausente o cabeceras de libbpf no encontradas; fije TC_PREFIX): solo se construyeron policy.o y events.o. make termina con exito (exit 0) por ausencia de toolchain."
endif

$(BUILD):
	mkdir -p $(BUILD)

# Objetos de usuario sin libbpf: solo gcc.
$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS_USER) -c $< -o $@

# Cadena BPF.
$(VMLINUX): | $(BUILD)
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

$(BPF_OBJ): bpf/mmapguard.bpf.c include/mmapguard_common.h $(VMLINUX) | $(BUILD)
	$(CLANG) $(CFLAGS_BPF) -c $< -o $@

$(BPF_LINK): $(BPF_OBJ)
	$(BPFTOOL) gen object $@ $<

$(SKEL): $(BPF_LINK)
	$(BPFTOOL) gen skeleton $< > $@

# Binario: el skeleton se incluye como cabecera de sistema (-isystem) para no fallar por codigo generado.
$(BIN): src/mmapguard.c src/policy.c src/events.c $(HDRS) $(SKEL) | $(BUILD)
	$(CC) $(CFLAGS_USER) -isystem $(BUILD) -I$(TC_PREFIX)/usr/include src/mmapguard.c src/policy.c src/events.c -o $@ -l:libbpf.so.1

# Pruebas: no dependen de la toolchain BPF. Las que cargan la defensa exigen root (ver tests/run_tests.sh).
test:
	@MMAPGUARD="$${MMAPGUARD:-$(abspath $(BIN))}" bash tests/run_tests.sh

clean:
	rm -rf $(BUILD)

help:
	@echo "make all    construye bpf y user (por defecto)"
	@echo "make bpf    vmlinux.h, mmapguard.bpf.o, mmapguard.o y mmapguard.skel.h en build/"
	@echo "make user   build/policy.o, build/events.o y build/mmapguard (si hay skeleton)"
	@echo "make test   bash tests/run_tests.sh"
	@echo "make clean  borra build/"
	@echo "Variables: TC_PREFIX=$(TC_PREFIX) CLANG=$(CLANG) BPFTOOL=$(BPFTOOL)"

# Medicion de costos (linea base, sin defensa): no necesita toolchain BPF ni root.
# La medicion con defensa se hace aparte: sudo bash bench/bench_con_defensa.sh
$(BUILD)/bench_mmap: bench/bench_mmap.c | $(BUILD)
	$(CC) -Wall -Wextra -Werror -std=gnu11 -O2 $< -o $@

bench: $(BUILD)/bench_mmap
	@bash bench/bench.sh
