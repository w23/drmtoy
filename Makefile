.SUFFIXES:
.DEFAULT:

BUILDDIR ?= build
CC ?= cc
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -std=gnu99 -I/usr/include/libdrm
LIBS += -ldrm -lGL

ifeq ($(DEBUG), 1)
	CONFIG = dbg
	CFLAGS += -O0 -ggdb3
else
	CONFIG = rel
	CFLAGS += -O3
endif

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) -std=gnu99 $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

OBJDIR ?= $(BUILDDIR)/$(CONFIG)

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

ENUM_SOURCES = enum.c
ENUM_OBJS = $(ENUM_SOURCES:%=$(OBJDIR)/%.o)
ENUM_DEPS = $(ENUM_OBJS:%=%.d)
-include $(ENUM_DEPS)
enum: $(OBJDIR)/enum
$(OBJDIR)/enum: $(ENUM_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ $(LIBS) -o $@

KMSGRAB_SOURCES = kmsgrab.c
KMSGRAB_OBJS = $(KMSGRAB_SOURCES:%=$(OBJDIR)/%.o)
KMSGRAB_DEPS = $(KMSGRAB_OBJS:%=%.d)
-include $(KMSGRAB_DEPS)
kmsgrab: $(OBJDIR)/kmsgrab
$(OBJDIR)/kmsgrab: $(KMSGRAB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ $(LIBS) -lEGL -lX11 -o $@

DRMSEND_SOURCES = drmsend.c
DRMSEND_OBJS = $(DRMSEND_SOURCES:%=$(OBJDIR)/%.o)
DRMSEND_DEPS = $(DRMSEND_OBJS:%=%.d)
-include $(DRMSEND_DEPS)
drmsend: $(OBJDIR)/drmsend
$(OBJDIR)/drmsend: $(DRMSEND_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ $(LIBS) -lEGL -lX11 -o $@
