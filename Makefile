default: main bench quantize server

ifndef UNAME_S
UNAME_S := $(shell uname -s)
endif

ifndef UNAME_P
UNAME_P := $(shell uname -p)
endif

ifndef UNAME_M
UNAME_M := $(shell uname -m)
endif

ifndef NVCC_VERSION
	ifeq ($(call,$(shell which nvcc))$(.SHELLSTATUS),0)
		NVCC_VERSION := $(shell nvcc --version | egrep -o "V[0-9]+.[0-9]+.[0-9]+" | cut -c2-)
	endif
endif

CCV  := $(shell $(CC) --version | head -n 1)
CXXV := $(shell $(CXX) --version | head -n 1)

# Mac OS + Arm can report x86_64
# ref: https://github.com/ggerganov/whisper.cpp/issues/66#issuecomment-1282546789
ifeq ($(UNAME_S),Darwin)
	ifneq ($(UNAME_P),arm)
		SYSCTL_M := $(shell sysctl -n hw.optional.arm64)
		ifeq ($(SYSCTL_M),1)
			# UNAME_P := arm
			# UNAME_M := arm64
			warn := $(warning Your arch is announced as x86_64, but it seems to actually be ARM64. Not fixing that can lead to bad performance. For more info see: https://github.com/ggerganov/whisper.cpp/issues/66\#issuecomment-1282546789)
		endif
	endif
endif

#
# Compile flags
#

CFLAGS   = -I.              -O3 -DNDEBUG -std=c11   -fPIC
CXXFLAGS = -I. -I./examples -O3 -DNDEBUG -std=c++11 -fPIC
LDFLAGS  =

# clock_gettime came in POSIX.1b (1993)
# CLOCK_MONOTONIC came in POSIX.1-2001 / SUSv3 as optional
# posix_memalign came in POSIX.1-2001 / SUSv3
# M_PI is an XSI extension since POSIX.1-2001 / SUSv3, came in XPG1 (1985)
CFLAGS   += -D_XOPEN_SOURCE=600
CXXFLAGS += -D_XOPEN_SOURCE=600

# Somehow in OpenBSD whenever POSIX conformance is specified
# some string functions rely on locale_t availability,
# which was introduced in POSIX.1-2008, forcing us to go higher
ifeq ($(UNAME_S),OpenBSD)
	CFLAGS   += -U_XOPEN_SOURCE -D_XOPEN_SOURCE=700
	CXXFLAGS += -U_XOPEN_SOURCE -D_XOPEN_SOURCE=700
endif

# Data types, macros and functions related to controlling CPU affinity
# are available on Linux through GNU extensions in libc
ifeq ($(UNAME_S),Linux)
	CFLAGS   += -D_GNU_SOURCE
	CXXFLAGS += -D_GNU_SOURCE
endif

# RLIMIT_MEMLOCK came in BSD, is not specified in POSIX.1,
# and on macOS its availability depends on enabling Darwin extensions
# similarly on DragonFly, enabling BSD extensions is necessary
ifeq ($(UNAME_S),Darwin)
	CFLAGS   += -D_DARWIN_C_SOURCE
	CXXFLAGS += -D_DARWIN_C_SOURCE
endif
ifeq ($(UNAME_S),DragonFly)
	CFLAGS   += -D__BSD_VISIBLE
	CXXFLAGS += -D__BSD_VISIBLE
endif

# alloca is a non-standard interface that is not visible on BSDs when
# POSIX conformance is specified, but not all of them provide a clean way
# to enable it in such cases
ifeq ($(UNAME_S),FreeBSD)
	CFLAGS   += -D__BSD_VISIBLE
	CXXFLAGS += -D__BSD_VISIBLE
endif
ifeq ($(UNAME_S),NetBSD)
	CFLAGS   += -D_NETBSD_SOURCE
	CXXFLAGS += -D_NETBSD_SOURCE
endif
ifeq ($(UNAME_S),OpenBSD)
	CFLAGS   += -D_BSD_SOURCE
	CXXFLAGS += -D_BSD_SOURCE
endif

# OS specific
# TODO: support Windows
ifeq ($(filter $(UNAME_S),Linux Darwin DragonFly FreeBSD NetBSD OpenBSD Haiku),$(UNAME_S))
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif

# detect Windows
ifneq ($(findstring _NT,$(UNAME_S)),)
	_WIN32 := 1
endif

# Windows Sockets 2 (Winsock) for network-capable apps
ifeq ($(_WIN32),1)
	LWINSOCK2 := -lws2_32
endif

# Architecture specific
# TODO: probably these flags need to be tweaked on some architectures
#       feel free to update the Makefile for your architecture and send a pull request or issue
ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 i686 amd64))
	ifeq ($(UNAME_S),Darwin)
		CPUINFO_CMD := sysctl machdep.cpu.features machdep.cpu.leaf7_features
	else ifeq ($(UNAME_S),Linux)
		CPUINFO_CMD := cat /proc/cpuinfo
	else ifneq (,$(filter MINGW32_NT% MINGW64_NT%,$(UNAME_S)))
		CPUINFO_CMD := cat /proc/cpuinfo
	else ifneq (,$(filter DragonFly FreeBSD,$(UNAME_S)))
		CPUINFO_CMD := grep Features /var/run/dmesg.boot
	else ifeq ($(UNAME_S),Haiku)
		CPUINFO_CMD := sysinfo -cpu
	endif

	ifdef CPUINFO_CMD
		AVX_M := $(shell $(CPUINFO_CMD) | grep -iwE 'AVX|AVX1.0')
		ifneq (,$(AVX_M))
			CFLAGS   += -mavx
			CXXFLAGS += -mavx
		endif

		AVX2_M := $(shell $(CPUINFO_CMD) | grep -iw 'AVX2')
		ifneq (,$(AVX2_M))
			CFLAGS   += -mavx2
			CXXFLAGS += -mavx2
		endif

		FMA_M := $(shell $(CPUINFO_CMD) | grep -iw 'FMA')
		ifneq (,$(FMA_M))
			CFLAGS   += -mfma
			CXXFLAGS += -mfma
		endif

		F16C_M := $(shell $(CPUINFO_CMD) | grep -iw 'F16C')
		ifneq (,$(F16C_M))
			CFLAGS   += -mf16c
			CXXFLAGS += -mf16c
		endif

		SSE3_M := $(shell $(CPUINFO_CMD) | grep -iwE 'PNI|SSE3')
		ifneq (,$(SSE3_M))
			CFLAGS   += -msse3
			CXXFLAGS += -msse3
		endif

		SSSE3_M := $(shell $(CPUINFO_CMD) | grep -iw 'SSSE3')
		ifneq (,$(SSSE3_M))
			CFLAGS   += -mssse3
			CXXFLAGS += -mssse3
		endif
	endif
endif

ifneq ($(filter ppc64%,$(UNAME_M)),)
	POWER9_M := $(shell grep "POWER9" /proc/cpuinfo)
	ifneq (,$(findstring POWER9,$(POWER9_M)))
		CFLAGS += -mpower9-vector
	endif
	# Require c++23's std::byteswap for big-endian support.
	ifeq ($(UNAME_M),ppc64)
		CXXFLAGS += -std=c++23 -DGGML_BIG_ENDIAN
	endif
endif

ifndef WHISPER_NO_ACCELERATE
	# Mac M1 - include Accelerate framework
	ifeq ($(UNAME_S),Darwin)
		CFLAGS  += -DGGML_USE_ACCELERATE
		LDFLAGS += -framework Accelerate
	endif
endif

ifdef WHISPER_COREML
	CXXFLAGS += -DWHISPER_USE_COREML
	LDFLAGS  += -framework Foundation -framework CoreML

ifdef WHISPER_COREML_ALLOW_FALLBACK
	CXXFLAGS += -DWHISPER_COREML_ALLOW_FALLBACK
endif
endif

ifndef WHISPER_NO_METAL
	ifeq ($(UNAME_S),Darwin)
		WHISPER_METAL := 1

		CFLAGS   += -DGGML_USE_METAL
		CXXFLAGS += -DGGML_USE_METAL
		LDFLAGS  += -framework Foundation -framework Metal -framework MetalKit
	endif
endif

ifdef WHISPER_OPENBLAS
	CFLAGS  += -DGGML_USE_OPENBLAS -I/usr/local/include/openblas -I/usr/include/openblas
	LDFLAGS += -lopenblas
endif

ifdef WHISPER_CUBLAS
	ifeq ($(shell expr $(NVCC_VERSION) \>= 11.6), 1)
		CUDA_ARCH_FLAG=native
	else
		CUDA_ARCH_FLAG=all
	endif

	CFLAGS      += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/$(UNAME_M)-linux/include
	CXXFLAGS    += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/$(UNAME_M)-linux/include
	LDFLAGS     += -lcuda -lcublas -lculibos -lcudart -lcublasLt -lpthread -ldl -lrt -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -L$(CUDA_PATH)/targets/$(UNAME_M)-linux/lib
	WHISPER_OBJ += ggml-cuda.o
	NVCC        = nvcc
	NVCCFLAGS   = --forward-unknown-to-host-compiler -arch=$(CUDA_ARCH_FLAG)

ggml-cuda.o: libs/ggml-cuda.cu libs/ggml-cuda.h
	$(NVCC) $(NVCCFLAGS) $(CXXFLAGS) -Wno-pedantic -c $< -o $@
endif

ifdef WHISPER_HIPBLAS
	ROCM_PATH   ?= /opt/rocm
	HIPCC       ?= $(ROCM_PATH)/bin/hipcc
	GPU_TARGETS ?= $(shell $(ROCM_PATH)/llvm/bin/amdgpu-arch)
	CFLAGS      += -DGGML_USE_HIPBLAS -DGGML_USE_CUBLAS
	CXXFLAGS    += -DGGML_USE_HIPBLAS -DGGML_USE_CUBLAS
	LDFLAGS     += -L$(ROCM_PATH)/lib -Wl,-rpath=$(ROCM_PATH)/lib
	LDFLAGS     += -lhipblas -lamdhip64 -lrocblas
	HIPFLAGS    += $(addprefix --offload-arch=,$(GPU_TARGETS))
	WHISPER_OBJ += ggml-cuda.o

ggml-cuda.o: libs/ggml-cuda.cu libs/ggml-cuda.h
	$(HIPCC) $(CXXFLAGS) $(HIPFLAGS) -x hip -c -o $@ $<
endif

ifdef WHISPER_CLBLAST
	CFLAGS 		+= -DGGML_USE_CLBLAST
	CXXFLAGS 	+= -DGGML_USE_CLBLAST
	LDFLAGS	 	+= -lclblast
	ifeq ($(UNAME_S),Darwin)
		LDFLAGS	 	+= -framework OpenCL
	else
		LDFLAGS	    += -lOpenCL
	endif
	WHISPER_OBJ	+= ggml-opencl.o

ggml-opencl.o: libs/ggml-opencl.cpp libs/ggml-opencl.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
endif

ifdef WHISPER_GPROF
	CFLAGS   += -pg
	CXXFLAGS += -pg
endif

ifneq ($(filter aarch64%,$(UNAME_M)),)
	CFLAGS   += -mcpu=native
	CXXFLAGS += -mcpu=native
endif

ifneq ($(filter armv6%,$(UNAME_M)),)
	# 32-bit Raspberry Pi 1, 2, 3
	CFLAGS += -mfpu=neon -mfp16-format=ieee -mno-unaligned-access
endif

ifneq ($(filter armv7%,$(UNAME_M)),)
	# 32-bit ARM, for example on Armbian or possibly raspbian
	#CFLAGS   += -mfpu=neon -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access
	#CXXFLAGS += -mfpu=neon -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access

	# 64-bit ARM on 32-bit OS, use these (TODO: auto-detect 64-bit)
	CFLAGS   += -mfpu=neon-fp-armv8 -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access
	CXXFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access
endif

ifneq ($(filter armv8%,$(UNAME_M)),)
	# Raspberry Pi 4
	CFLAGS   += -mfpu=neon-fp-armv8 -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access
	CXXFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -funsafe-math-optimizations -mno-unaligned-access
endif

#
# Print build information
#

$(info I whisper.cpp build info: )
$(info I UNAME_S:  $(UNAME_S))
$(info I UNAME_P:  $(UNAME_P))
$(info I UNAME_M:  $(UNAME_M))
$(info I CFLAGS:   $(CFLAGS))
$(info I CXXFLAGS: $(CXXFLAGS))
$(info I LDFLAGS:  $(LDFLAGS))
$(info I CC:       $(CCV))
$(info I CXX:      $(CXXV))
$(info )

#
# Build library
#

ggml.o: libs/ggml.c libs/ggml.h libs/ggml-cuda.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-alloc.o: libs/ggml-alloc.c libs/ggml.h libs/ggml-alloc.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-backend.o: libs/ggml-backend.c libs/ggml.h libs/ggml-backend.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-quants.o: libs/ggml-quants.c libs/ggml.h libs/ggml-quants.h
	$(CC)  $(CFLAGS)   -c $< -o $@

WHISPER_OBJ += ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o

whisper.o: libs/whisper.cpp libs/whisper.h libs/ggml.h libs/ggml-cuda.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifndef WHISPER_COREML
WHISPER_OBJ += whisper.o
else
whisper-encoder.o: coreml/whisper-encoder.mm coreml/whisper-encoder.h
	$(CXX) -O3 -I . -fobjc-arc -c coreml/whisper-encoder.mm -o whisper-encoder.o

whisper-encoder-impl.o: coreml/whisper-encoder-impl.m coreml/whisper-encoder-impl.h
	$(CXX) -O3 -I . -fobjc-arc -c coreml/whisper-encoder-impl.m -o whisper-encoder-impl.o

WHISPER_OBJ += whisper.o whisper-encoder.o whisper-encoder-impl.o
endif

ifdef WHISPER_METAL
ggml-metal.o: libs/ggml-metal.m libs/ggml-metal.h
	$(CC) $(CFLAGS) -c $< -o $@

WHISPER_OBJ += ggml-metal.o
endif

libwhisper.a: $(WHISPER_OBJ)
	$(AR) rcs libwhisper.a $(WHISPER_OBJ)

libwhisper.so: $(WHISPER_OBJ)
	$(CXX) $(CXXFLAGS) -shared -o libwhisper.so $(WHISPER_OBJ) $(LDFLAGS)

clean:
	rm -f *.o main stream command talk talk-llama bench quantize server lsp libwhisper.a libwhisper.so

#
# Examples
#

CC_SDL=`sdl2-config --cflags --libs`

SRC_COMMON     = examples/common.cpp examples/common-ggml.cpp
SRC_COMMON_SDL = examples/common-sdl.cpp


stream: examples/stream/stream.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ)
	$(CXX) $(CXXFLAGS) examples/stream/stream.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ) -o stream $(CC_SDL) $(LDFLAGS)

command: examples/command/command.cpp examples/grammar-parser.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ)
	$(CXX) $(CXXFLAGS) examples/command/command.cpp examples/grammar-parser.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ) -o command $(CC_SDL) $(LDFLAGS)

talk: examples/talk/talk.cpp examples/talk/gpt-2.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ)
	$(CXX) $(CXXFLAGS) examples/talk/talk.cpp examples/talk/gpt-2.cpp $(SRC_COMMON) $(SRC_COMMON_SDL) $(WHISPER_OBJ) -o talk $(CC_SDL) $(LDFLAGS)
