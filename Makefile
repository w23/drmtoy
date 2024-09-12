.SUFFIXES:
.DEFAULT:
.EXTRA_PREREQS += Makefile
MAKEFLAGS += -r --no-print-directory

all: enum dumbkms

BUILDDIR ?= build
CC ?= cc
CFLAGS += -Wall -Wextra -Werror -pedantic
CFLAGS += -std=gnu99 -I/usr/include/libdrm
LIBS += -ldrm

ifeq ($(DEBUG), 1)
	CFLAGS += -O0 -ggdb3
else
	CFLAGS += -O3
endif

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

OBJDIR ?= $(BUILDDIR)/obj

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

SOURCES = \
	enum.c \
	dumbkms.c \

	#kmsgrab.c \
	#drmsend.c \

OBJS = $(SOURCES:%=$(OBJDIR)/%.o)
DEPS = $(OBJS:%=%.d)
-include $(DEPS)

enum: $(BUILDDIR)/enum
dumbkms: $(BUILDDIR)/dumbkms
#drmsend: $(BUILDDIR)/drmsend
#kmsgrab: $(BUILDDIR)/kmsgrab

$(BUILDDIR)/%: $(OBJDIR)/%.c.o
	@mkdir -p $(dir $@)
	$(CC) $^ $(LIBS) -o $@

#$(BUILDDIR)/kmsgrab: $(OBJDIR)/kmsgrab.c.o
#	@mkdir -p $(dir $@)
#	$(CC) $^ $(LIBS) -lEGL -lX11 -o $@
#
#$(BUILDDIR)/drmsend: $(OBJDIR)/%.c.o
#	@mkdir -p $(dir $@)
#	$(CC) $^ $(LIBS) -lEGL -lX11 -o $@
