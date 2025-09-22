SRCDIR := ..
OBJDIR := .

CFLAGS = -Wall -DDEBUG -Dvariable="value with space" -I $(SRCDIR)/include
LDFLAGS =
PROGRAM = $(OBJDIR)/prg

$(OBJDIR)/main.o: $(SRCDIR)/main.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/main.c

$(OBJDIR)/clean-one.o: $(SRCDIR)/clean-one.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/clean-one.c

$(OBJDIR)/clean-two.o: $(SRCDIR)/clean-two.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/clean-two.c

$(OBJDIR)/emit-one.o: $(SRCDIR)/emit-one.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/emit-one.c

$(OBJDIR)/emit-two.o: $(SRCDIR)/emit-two.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/emit-two.c

$(OBJDIR)/broken-one.o: $(SRCDIR)/broken-one.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/broken-one.c

$(OBJDIR)/broken-two.o: $(SRCDIR)/broken-two.c
	$(CC) $(CFLAGS) -c -o $@ $(SRCDIR)/broken-two.c

$(PROGRAM): $(OBJDIR)/main.o $(OBJDIR)/clean-one.o $(OBJDIR)/clean-two.o $(OBJDIR)/emit-one.o $(OBJDIR)/emit-two.o
	$(CC) $(LDFLAGS) -o $@ $(OBJDIR)/main.o $(OBJDIR)/clean-one.o $(OBJDIR)/clean-two.o $(OBJDIR)/emit-one.o $(OBJDIR)/emit-two.o

build_regular: $(PROGRAM)

build_clean: $(OBJDIR)/main.o $(OBJDIR)/clean-one.o $(OBJDIR)/clean-two.o

build_broken: $(OBJDIR)/main.o $(OBJDIR)/broken-one.o $(OBJDIR)/broken-two.o

build_all_in_one: $(SRCDIR)/main.c $(SRCDIR)/clean-one.c $(SRCDIR)/clean-two.c $(SRCDIR)/emit-one.c $(SRCDIR)/emit-two.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(PROGRAM) $(SRCDIR)/main.c $(SRCDIR)/clean-one.c $(SRCDIR)/clean-two.c $(SRCDIR)/emit-one.c $(SRCDIR)/emit-two.c

clean:
	rm -f $(PROGRAM) $(OBJDIR)/*.o
