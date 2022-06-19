#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>

#include <errno.h>
#include <stdarg.h>
static _Noreturn void 
#if defined(__clang__) || defined(__GNUC__)
__attribute__ ((format (printf, 1, 2)))
#endif
die(char *fmt, ...)
{
	int e = errno;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	if (e!= 0) fprintf(stderr, " (errno %d: %s)", e, strerror(e));
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

static void 
usage (char * progname) 
{
	fprintf(stderr, "Usage: %s command file\n", progname);
	fprintf(stderr, "where,\n");
	fprintf(stderr, "\tcommand   is 'as' (to assemble a source code file) or 'run' (to interpret bytecode)\n");
	fprintf(stderr, "\tfile      is the filename to operate on\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "When assembling, bytecode is written to stdout, so piping to a file is advised.\n");
	fprintf(stderr, "\n");
	exit (EXIT_SUCCESS);
}

static void assemble (char    file[], size_t sz);
static void run      (int64_t mem[],  size_t sz);

int 
main (int argc, char ** argv)
{
	if (argc < 3) usage(argv[0]);

	const char * action = argv[1];
	const char * file   = argv[2];

	static int64_t inputfile[1<<22] = {};

	if (!strcmp(action, "as")) {

		FILE * f = fopen(file, "r");
		if (!f) die("Couldn't open %s", file);
		size_t n = fread(inputfile, 1, sizeof(inputfile)/sizeof(inputfile[0]), f);
		fclose(f);
		
		if(n == sizeof(inputfile)) die("input file too big (/implementation of uvm too janky)");

		assemble((char*)inputfile, sizeof(inputfile));

		exit (EXIT_SUCCESS);
	}

	if (!strcmp(action, "run")) {

		FILE * f = fopen(file, "r");
		if (!f) die("Couldn't open %s", file);
		size_t n = fread(inputfile, 1, sizeof(inputfile)/sizeof(inputfile[0]), f);
		fclose(f);
		
		if(n == sizeof(inputfile)) die("input file too big (/implementation of uvm too janky)");

		run(inputfile, sizeof(inputfile));

		exit (EXIT_SUCCESS);
	}

	usage(argv[0]);
}


typedef enum {
	OP_STOP = 0,
	OP_GET,
	OP_PUT,
	OP_LD,
	OP_ST,
	OP_ADD,
	OP_SUB,
	OP_JP,
	OP_JZ,
	OP_J,
	OP_CONST,
} opcodes;

typedef struct {
	int opcode;
	const char * str;
	int args; 
} op_t;

const op_t known_ops[] = {
	[OP_STOP] = {OP_STOP, "stop", 0},
	[OP_GET]  = {OP_GET,  "get",  0},
	[OP_PUT]  = {OP_PUT,  "put",  0},
	[OP_LD]   = {OP_LD,   "ld",   1},
	[OP_ST]   = {OP_ST,   "st",   1},
	[OP_ADD]  = {OP_ADD,  "add",  1},
	[OP_SUB]  = {OP_SUB,  "sub",  1},
	[OP_JP]   = {OP_JP,   "jp",   1},
	[OP_JZ]   = {OP_JZ,   "jz",   1},
	[OP_J]    = {OP_J,    "j",    1},
};



static void
run (int64_t mem[], size_t sz) 
{
	int64_t pc  = 0;
	int64_t reg = 0;

	char buf[1024] = {};
	
	for (pc = 0; pc >= 0; ) {

		int64_t op   = mem[pc];
		int64_t addr = pc == sz-1 ? 0 : mem[pc+1];
		pc += 2;

		switch (op) {
		case OP_STOP:
			pc = -1;
			break;
		case OP_GET:
			printf("enter a number: ");
			if (fgets(buf,sizeof(buf),stdin)) 
				reg = atoi(buf);
			else 
				pc = -1; 
			break;
		case OP_PUT:
			printf("%"PRIi64"\n", reg);
			break;
		case OP_LD:
			reg = mem[addr];
			break;
		case OP_ST:
			mem[addr] = reg;
			break;
		case OP_ADD:
			reg += mem[addr];
			break;
		case OP_SUB:
			reg -= mem[addr];
			break;
		case OP_JP:
			if (reg >  0) pc = addr;
			break;
		case OP_JZ:
			if (reg == 0) pc = addr;
			break;
		case OP_J:
			pc = addr;
			break;
		default:
			die("illegal instruction");
		}
	}
}



typedef enum {
	END = 0,
	ID,
	COLON,
	NUMBER,
	NEWLINE,
} token_e;

#define MAXID 21

typedef struct
{
	token_e tok;
	union {
		int64_t num;
		char id[MAXID];
	};
} token_t;

static void
dbg_print_token(token_t tok) {
	
	switch(tok.tok) {
	case ID:
		printf("ID %s\n", tok.id);
		break;
	case COLON:
		printf("COLON\n");
		break;
	case NUMBER:
		printf("NUMBER %" PRIi64 "\n", tok.num);
		break;
	case NEWLINE:
		printf("NEWLINE\n");
		break;
	case END:
		printf("END OF INPUT\n");
		break;
	default:
		die("<<<BUG>>>");
	}
}

static token_t
lex_eat(char **t) {

	while (isspace(**t) && **t != '\n') (*t)++;

	if (**t == '#') {
		// skip comments
		while (**t != '\n' && **t) (*t)++;
	}

	if (**t == ':') {
		(*t)++;
		return (token_t) { .tok = COLON };
	}

	if (**t == '\n') {
		(*t)++;
		return (token_t) { .tok = NEWLINE };
	}

	if (
		( (**t == '+' || **t == '-') && isdigit(*t[1]) ) 
		|| isdigit(**t)
		
	   ) {
	
		int sign = 1;
		if (**t == '-') { sign = -1; (*t)++; }
		if (**t == '+') { sign =  1; (*t)++; }		

		uint64_t val = 0;
		while (isdigit(**t)) {
			val = val * 10 + ((**t)-'0');
			(*t)++;
		}

		val *= sign;

		return (token_t) { .tok = NUMBER, .num = val };
	}

	if (isalnum(**t) || **t == '_') {

		token_t dummy = {.tok = ID};
		char * q = *t;
		for (unsigned i = 0; i < sizeof(dummy.id)-1; i++) {

			if (isalnum(**t) || **t == '_') {
				dummy.id[i] = **t;
				(*t)++;
			}
			else return dummy;
		}

		die("identifier too long: %.20s", q);
	}


	if (**t == 0) return (token_t) { .tok = END };

	die ("invalid token: %.20s", *t);

}

static token_t 
lex_peek (char **t) {
	char * u = *t;
	token_t tok = lex_eat(&u);
	return tok;
}


typedef struct {
	int64_t addr;
	char    id[MAXID];
} sym;


static struct {
	int64_t mem[1<<20];
	sym symtab[1<<20];
	int nsym;
	int nextmem;
} state = {};


static void 
expect_newline (char **t)
{
	char *u = *t;
	token_t tok = lex_eat(t);
	if (tok.tok != NEWLINE)
		die("newline expected: %.40s", u);
}

static int 
parse_label(char **t, int pass) {
	char *u = *t;
	token_t tok1 = lex_eat(&u);
	token_t tok2 = lex_eat(&u);

	if (tok1.tok == ID && tok2.tok == COLON) {
		*t = u;

		if (pass == 1) {
			memcpy(state.symtab[state.nsym].id, tok1.id, sizeof(tok1.id));
			state.symtab[state.nsym++].addr = state.nextmem;
		} else if (pass == 2) {
		} else die ("???");

		return 1;
	}

	return 0;

}

static token_t 
expect_id (char **t) 
{
	char *u = *t;
	token_t tok = lex_eat(t);
	if (tok.tok != ID)
		die("identifier expected: %.40s", u);
	return tok;
}

static int64_t 
symtab_lookup(char * id) 
{
	for (unsigned u = 0; u < state.nsym; u++) {
		if (!strcmp(id, state.symtab[u].id)) {
			return state.symtab[u].addr;
		}
	}
	die("symtab lookup failed");
}

static int
parse_instruction(char **t, int pass) 
{
	token_t tok = lex_peek(t);
	if (tok.tok != ID) return 0;

	for (unsigned u = 0; u < sizeof(known_ops)/sizeof(known_ops[0]); u++) {

		if (!strcmp(tok.id, known_ops[u].str)) {
			lex_eat(t);

			if (pass == 1) {
				state.nextmem += 2;
				if (known_ops[u].args) expect_id(t);

			} else if (pass == 2) {
	
				state.mem[state.nextmem++] = known_ops[u].opcode;	
				if (known_ops[u].args) {
					token_t arg = expect_id(t);
					state.mem[state.nextmem++] = symtab_lookup(arg.id);
				} else {
					state.mem[state.nextmem++] = 0;
				}

			} else die ("???");

			return 1;
		}

	}

	return 0;
}

static int
parse_const (char **t, int pass) 
{
	char *u = *t;

	token_t tok = lex_peek(t);
	if (tok.tok != ID) return 0;
	if (strcmp(tok.id, "const")) return 0; 
	lex_eat(t);
	token_t arg = lex_eat(t);

	if (arg.tok == NUMBER) {
		if (pass == 1) {
			state.nextmem++;
		} else if (pass == 2) {
			state.mem[state.nextmem++] = arg.num;
		} else die ("???");
		return 1;
	}

	die ("expected a number: %.40s", u);
}

static int 
parse_line(char **t, int pass)
{

	if (parse_label(t, pass)) {
		// optional instruction following label
		parse_instruction(t, pass) || 
		parse_const(t, pass);
		expect_newline(t);
		return 1;
	}

	if (parse_instruction(t, pass)) {
		expect_newline(t);
		return 1;
	}

	if (parse_const(t, pass)) {
		expect_newline(t);
		return 1;
	}

	char *u = *t;
	token_t tok = lex_eat(&u);
	if (tok.tok == NEWLINE || tok.tok == END) {
		*t = u;
		return 1;	
	}

	return 0;
}

static void 
assemble (char file[], size_t sz)
{
	// while ((tok = lex(&t)).tok != END) dbg_print_token(tok);

	// Assembler pass 1

	char * t = file;
	while (*t) {
		char *u = t;
		if(!parse_line(&t, 1))
			die("pass 1, invalid line: %.40s", u);
	}

	// Assembler pass 2
	state.nextmem = 0;

	t = file;
	while (*t) {
		char *u = t;
		if(!parse_line(&t, 2))
			die("pass 2, invalid line: %.40s", u);
	}

	fwrite(state.mem, 1, sizeof(state.mem[0])*state.nextmem, stdout);
}

