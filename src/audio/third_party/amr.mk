# opencore-amr narrowband decoder (Apache-2.0): C code in .cpp files, compiled as C.
# Included by Makefile.host and Makefile.psp; set AMR_OBJDIR before including.
AMR_DIR = src/audio/third_party/opencore-amr
AMR_BASE = $(AMR_DIR)/opencore/codecs_v2/audio/gsm_amr
AMR_INC = -I$(AMR_DIR)/oscl -I$(AMR_BASE)/amr_nb/dec/src -I$(AMR_BASE)/amr_nb/common/include \
          -I$(AMR_BASE)/amr_nb/dec/include -I$(AMR_BASE)/common/dec/include
AMR_SRC = $(shell find $(AMR_DIR) -name '*.cpp')
AMR_OBJ = $(patsubst src/%.cpp,$(AMR_OBJDIR)/%.o,$(AMR_SRC))
# fixed-point DSP code: relies on two's-complement shifts/overflow, so make that defined behaviour
AMR_CFLAGS = -x c -std=c99 -fwrapv -w $(AMR_INC)
