# Example makefile for CPE 464
#

CC = gcc
CFLAGS = -g -Wall -Werror
OS = $(shell uname -s)
PROC = $(shell uname -p)
EXEC_SUFFIX=$(OS)-$(PROC)

ifeq ("$(OS)", "SunOS")
	OSLIB=-L/opt/csw/lib -R/opt/csw/lib -lsocket -lnsl
	OSINC=-I/opt/csw/include
	OSDEF=-DSOLARIS
else
ifeq ("$(OS)", "Darwin")
	OSLIB=
	OSINC=
	OSDEF=-DDARWIN
else
	OSLIB=
	OSINC=
	OSDEF=-DLINUX
endif
endif

all:  json-server-$(EXEC_SUFFIX)

json-server-$(EXEC_SUFFIX): json-server.c
	$(CC) $(CFLAGS) $(OSINC) $(OSLIB) $(OSDEF) -lpcap -o $@ json-server.c checksum.c smartalloc.c

test-env: test-env-$(EXEC_SUFFIX)
test-env-Linux-x86_64:
	tmux new-session -s "server_test" -d;
	tmux split-window -v;
	tmux select-pane -t 0;
	tmux set -g mouse-select-pane on;
	tmux set -g mouse-resize-pane on;
	tmux send-keys "./json-server-Linux-x86_64" C-m;
	tmux select-pane -t 1;
	tmux attach-session -t;
test-env-Darwin-arm:
	tmux new-session -s "server_test" -d;
	tmux split-window -v;
	tmux select-pane -t 0;
	tmux set -g mouse on;
	tmux send-keys "./json-server-Darwin-arm 127.0.0.1" C-m;
	tmux select-pane -t 1;
	tmux attach-session -t;

handin: README
	handin bellardo 464_p1 README smartalloc.c smartalloc.h checksum.c checksum.h json-server.c json-server.h Makefile /json

clean:
	-rm -rf json-server-* json-server-*.dSYM
