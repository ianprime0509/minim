/*
 * Copyright 2018 Ian Johnson
 *
 * This is free software, distributed under the MIT license.  A copy of the
 * license can be found in the LICENSE file in the project root, or at
 * https://opensource.org/licenses/MIT.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#ifndef BSD
/* Ported versions of BSD helper functions. */
void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);
void vwarn(const char *fmt, va_list args);
void vwarnx(const char *fmt, va_list args);
#else
#include <err.h>
#endif

/* The number of registers supported by the interpreter. */
#define N_REGS 256
/* The number of stacks supported by the interpreter. */
#define N_STACKS 256

static char *progname;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-e eof] [file]\n", progname);
	exit(1);
}

static inline void
progerr(size_t pc, char ch)
{
	errx(1, "error in program at position %zu (%c)", pc, ch);
}

/* Memory helpers. */
void *xrealloc(void *buf, size_t sz);

struct prog {
	char *data;
	size_t len;
};

/* Read all data from the given input FILE. */
int prog_read(struct prog *prog, FILE *file);

struct stack {
	uint8_t *data;
	size_t len;
	size_t cap;
};

int stack_peek(struct stack *stack, uint8_t *ret);
int stack_pop(struct stack *stack, uint8_t *ret);
int stack_pop2(struct stack *stack, uint8_t *upper, uint8_t *lower);
void stack_push(struct stack *stack, uint8_t val);

struct call_stack {
	size_t *data;
	size_t len;
	size_t cap;
};

int call_stack_pop(struct call_stack *stack, size_t *ret);
void call_stack_push(struct call_stack *stack, size_t val);

/*
 * Analogous to memchr(3), but finds the close delimiter matching the open
 * delimiter that is assumed to appear in the memory buffer just before the
 * given pointer.
 */
void *memdelim(const void *buf, int open, int close, size_t len);

int run(struct prog prog, uint8_t eof);

int
main(int argc, char **argv)
{
	struct prog prog;
	FILE *input;
	int opt;
	uint8_t eof = 0;

	progname = argv[0];
	memset(&prog, 0, sizeof prog);

	while ((opt = getopt(argc, argv, "e:")) != -1) {
		unsigned long num;
		char *numend;

		switch (opt) {
		case 'e':
			errno = 0;
			if (!(num = strtoul(optarg, &numend, 10)))
				errx(1, "invalid argument '%s' to -e", optarg);
			if (errno == ERANGE && num == ULONG_MAX)
				errx(1, "argument '%s' out of range", optarg);
			eof = num;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		input = stdin;
	} else if (argc == 1) {
		if (!strcmp(argv[0], "-"))
			input = stdin;
		else if (!(input = fopen(argv[0], "r")))
			err(1, "could not open '%s'\n", argv[0]);
	} else {
		usage();
	}

	if (prog_read(&prog, input))
		exit(1);
	if (input != stdin)
		fclose(input);

	return run(prog, eof);
}

int
run(struct prog prog, uint8_t eof)
{
	uint8_t regs[N_REGS] = {0};
	struct stack stacks[N_STACKS];
	size_t stack; /* Current stack. */
	struct call_stack squares, curlies;
	size_t pc; /* Program counter. */

	memset(&stacks, 0, sizeof stacks);
	memset(&squares, 0, sizeof squares);
	memset(&curlies, 0, sizeof curlies);

	stack = 0;
	pc = 0;
	while (pc < prog.len) {
		char ch = prog.data[pc];
		uint8_t arg0, arg1;
		size_t new_pc = pc + 1;

		if (isdigit(ch)) {
			stack_push(&stacks[stack], ch - '0');
			goto CONTINUE_PROGRAM;
		} else if (isalpha(ch)) {
			stack_push(&stacks[stack], ch);
			goto CONTINUE_PROGRAM;
		}

		switch (ch) {
		/* Arithmetic operators. */
		case '+':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 + arg1);
			break;
		case '-':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 - arg1);
			break;
		case '*':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 * arg1);
			break;
		case '/':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 / arg1);
			break;
		case '%':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 % arg1);
			break;
		case '&':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 & arg1);
			break;
		case '|':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 | arg1);
			break;
		case '^':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0 ^ arg1);
			break;

		/* Stack control operators. */
		case '_':
			if (stack_pop(&stacks[stack], NULL))
				progerr(pc, ch);
			break;
		case '#':
			if (stack_peek(&stacks[stack], &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg0);
			break;
		case '@':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], arg1);
			stack_push(&stacks[stack], arg0);
			break;
		case '>':
			stack = (stack + 1) % N_STACKS;
			break;
		case '<':
			stack = (stack + N_STACKS - 1) % N_STACKS;
			break;

		/* I/O operators. */
		case '.':
			if (stack_pop(&stacks[stack], &arg0))
				progerr(pc, ch);
			putchar(arg0);
			break;
		case ',': {
			int ch;
			/*
			 * By clearing the EOF indicator here, we guarantee
			 * portable behavior when new data is input on stdin
			 * after reaching EOF (e.g. the user pressed Ctrl+D,
			 * the Minim program didn't handle it, and then the
			 * user typed something else).  It appears that some
			 * implementations (including GNU/Linux) will return
			 * this new data with getchar(3) even if the EOF
			 * indicator is still set, but this may not be entirely
			 * portable (https://stackoverflow.com/a/4753925 seems
			 * to indicate that this behavior actually contradicts
			 * the standard, but I haven't been able to verify
			 * this).
			 */
			if (feof(stdin))
				clearerr(stdin);
			ch = getchar();
			stack_push(&stacks[stack], ch == EOF ? eof : ch);
		} break;
		case ';':
			if (stack_pop(&stacks[stack], &arg0))
				progerr(pc, ch);
			printf("%u ", arg0);
			break;

		/* Loop control operators. */
		case '[':
			if (stack_peek(&stacks[stack], &arg0))
				progerr(pc, ch);
			if (arg0 == 0) {
				char *close = memdelim(prog.data + pc + 1, '[',
				    ']', prog.len - pc - 1);
				if (!close)
					errx(1,
					    "'[' without matching ']' (position %zu)",
					    pc);
				new_pc = close - prog.data + 1;
			} else {
				call_stack_push(&squares, pc);
			}
			break;
		case ']': {
			size_t start;
			if (call_stack_pop(&squares, &start))
				errx(1,
				    "']' without matching '[' (position %zu)",
				    pc);
			new_pc = start;
		} break;
		case '{':
			if (stacks[stack].len == 0) {
				char *close = memdelim(prog.data + pc + 1, '{',
				    '}', prog.len - pc - 1);
				if (!close)
					errx(1, "'{' without matching '}' "
					        "(position %zu)",
					    pc);
				new_pc = close - prog.data + 1;
			} else {
				call_stack_push(&curlies, pc);
			}
			break;
		case '}': {
			size_t start;
			if (call_stack_pop(&curlies, &start))
				errx(1,
				    "'}' without matching '{' (position %zu)",
				    pc);
			new_pc = start;
		} break;

		/* Register operators. */
		case '=':
			if (stack_pop2(&stacks[stack], &arg1, &arg0))
				progerr(pc, ch);
			regs[arg0 % N_REGS] = arg1;
			break;
		case '$':
			if (stack_pop(&stacks[stack], &arg0))
				progerr(pc, ch);
			stack_push(&stacks[stack], regs[arg0 % N_REGS]);
			break;

		/* Literals. */
		case '"': {
			/* First character after the '"'. */
			const char *start = prog.data + pc + 1;
			const char *end = memchr(start, '"', prog.len - pc - 1);
			if (!end)
				errx(1,
				    "unclosed string literal (position %zu)",
				    pc);
			new_pc = end - prog.data + 1;
			while (--end >= start)
				stack_push(&stacks[stack], *end);
		} break;
		case '\'': {
			uint8_t n = 0;
			for (size_t i = pc + 1; i < prog.len; i++)
				if (prog.data[i] == '\'') {
					stack_push(&stacks[stack], n);
					new_pc = i + 1;
					goto CONTINUE_PROGRAM;
				} else if (isdigit(prog.data[i])) {
					n = 10 * n + (prog.data[i] - '0');
				} else {
					errx(1,
					    "unexpected character '%c' in numeric literal (position %zu)",
					    prog.data[i], pc);
				}
			errx(1, "unclosed numeric literal (position %zu)", pc);
		} break;
		}

	CONTINUE_PROGRAM:
		pc = new_pc;
	}

	/*
	 * The stack and program are intentionally not freed here, because it
	 * is unnecessary to do so (the operating system will reclaim the
	 * memory).
	 */
	exit(0);
}

void
*xrealloc(void *buf, size_t sz)
{
	void *new;
	if (!(new = realloc(buf, sz)))
		errx(2, "out of memory");
	return new;
}

int
prog_read(struct prog *prog, FILE *file)
{
	for (;;) {
		size_t read;

		prog->data = xrealloc(prog->data, prog->len + BUFSIZ);
		prog->len += read =
		    fread(prog->data + prog->len, 1, BUFSIZ, file);
		if (read < BUFSIZ) {
			if (ferror(file)) {
				warn("could not read program");
				return 1;
			} else if (feof(file)) {
				return 0;
			}
		}
	}
}

int
stack_peek(struct stack *stack, uint8_t *ret)
{
	if (stack->len == 0) {
		warnx("stack is empty");
		return 1;
	}
	if (ret)
		*ret = stack->data[stack->len - 1];
	return 0;
}

int
stack_pop(struct stack *stack, uint8_t *ret)
{
	if (stack->len == 0) {
		warnx("stack is empty");
		return 1;
	}
	stack->len--;
	if (ret)
		*ret = stack->data[stack->len];
	return 0;
}

int
stack_pop2(struct stack *stack, uint8_t *upper, uint8_t *lower)
{
	return stack_pop(stack, upper) || stack_pop(stack, lower);
}

void
stack_push(struct stack *stack, uint8_t val)
{
	if (stack->len == stack->cap) {
		stack->cap = stack->cap == 0 ? 1 : 2 * stack->cap;
		stack->data = xrealloc(stack->data, stack->cap);
	}
	stack->data[stack->len++] = val;
}

int
call_stack_pop(struct call_stack *stack, size_t *ret)
{
	if (stack->len == 0) {
		warnx("call stack is empty");
		return 1;
	}
	stack->len--;
	if (ret)
		*ret = stack->data[stack->len];
	return 0;
}

void
call_stack_push(struct call_stack *stack, size_t val)
{
	if (stack->len == stack->cap) {
		stack->cap = stack->cap == 0 ? 1 : 2 * stack->cap;
		stack->data = xrealloc(stack->data, stack->cap);
	}
	stack->data[stack->len++] = val;
}

void
*memdelim(const void *buf, int open, int close, size_t len)
{
	const unsigned char *b = buf;
	int level = 1; /* Delimiter nesting level. */

	while (len-- > 0) {
		if (*b == (unsigned char)open)
			level++;
		else if (*b == (unsigned char)close)
			level--;
		if (level == 0)
			return (void *)b;
		b++;
	}

	return NULL;
}

#ifndef BSD
/* BSD helper function definitions. */

void
err(int eval, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vwarn(fmt, args);
	va_end(args);
	exit(eval);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vwarnx(fmt, args);
	va_end(args);
	exit(eval);
}

void
warn(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vwarn(fmt, args);
	va_end(args);
}

void
warnx(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vwarnx(fmt, args);
	va_end(args);
}

void
vwarn(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", progname);
	if (fmt) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(errno));
}

void
vwarnx(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", progname);
	if (fmt)
		vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

#endif /* #ifndef BSD */
