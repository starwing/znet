walk = $1 $(foreach x,$(filter %/,$(wildcard $1*/)),$(call walk,$x))
examples := $(patsubst %.c,%,$(foreach x,$(call walk,examples/),$(wildcard $x*.c)))

LDLIBS := -lrt
CFLAGS := -O2 -Wall -pthread

.phony : all
all : ${examples}

.phony : clean
clean :
	-rm ${examples}
