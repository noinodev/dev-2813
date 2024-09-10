# build file directories
SRCDIR = src/
OBJDIR = out/
BINDIR = bin/

LOCALSRV = $(BINDIR)server

# obj sources
SRCSRV = $(OBJDIR)server.o $(OBJDIR)worker.o $(OBJDIR)auth.o

# compiler info
CC = gcc
CFLAGS = -c

# build server
server: $(SRCSRV)
	@echo "linking server"
	$(CC) $(SRCSRV) -o $(LOCALSRV) -lws2_32 -lpq

# object files for server
$(OBJDIR)server.o: $(SRCDIR)server.c
	$(CC) $(CFLAGS) $< -o $@

$(OBJDIR)worker.o: $(SRCDIR)worker.c
	$(CC) $(CFLAGS) $< -o $@

$(OBJDIR)auth.o: $(SRCDIR)auth.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJDIR)*.o $(LOCALSRV)