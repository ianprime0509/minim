const std = @import("std");

const Interpreter = struct {
    program: []const u8,
    pc: usize = 0,
    delims: std.AutoHashMapUnmanaged(usize, usize),
    literals: std.AutoHashMapUnmanaged(usize, u8),
    registers: [256]u8 = .{0} ** 256,
    stacks: [256]std.ArrayListUnmanaged(u8) = .{.{}} ** 256,
    stack: usize = 0,
    input: std.io.AnyReader,
    output: std.io.AnyWriter,
    allocator: std.mem.Allocator,

    pub const InitError = error{
        OutOfMemory,
        MismatchedLoop,
        MismatchedQuote,
        InvalidLiteral,
    };

    pub fn init(
        allocator: std.mem.Allocator,
        program: []const u8,
        input: std.io.AnyReader,
        output: std.io.AnyWriter,
    ) InitError!Interpreter {
        var delims: std.AutoHashMapUnmanaged(usize, usize) = .{};
        errdefer delims.deinit(allocator);
        var literals: std.AutoHashMapUnmanaged(usize, u8) = .{};
        errdefer literals.deinit(allocator);
        var loop_starts: std.ArrayListUnmanaged(struct { pc: usize, end: u8 }) = .{};
        defer loop_starts.deinit(allocator);
        var quote: ?struct { pc: usize, c: u8 } = null;
        for (program, 0..) |inst, pc| {
            if (quote) |q| {
                if (q.c == inst) {
                    try delims.put(allocator, q.pc, pc);
                    if (q.c == '\'') {
                        const str = program[q.pc + 1 .. pc];
                        try literals.put(allocator, q.pc, std.fmt.parseUnsigned(u8, str, 10) catch return error.InvalidLiteral);
                    }
                    quote = null;
                }
            } else switch (inst) {
                '[' => try loop_starts.append(allocator, .{ .pc = pc, .end = ']' }),
                '{' => try loop_starts.append(allocator, .{ .pc = pc, .end = '}' }),
                ']', '}' => |c| {
                    const loop_start = loop_starts.popOrNull() orelse return error.MismatchedLoop;
                    if (c != loop_start.end) return error.MismatchedLoop;
                    try delims.put(allocator, loop_start.pc, pc);
                    try delims.put(allocator, pc, loop_start.pc);
                },
                '\'', '"' => |c| quote = .{ .pc = pc, .c = c },
                else => {},
            }
        }
        if (loop_starts.items.len != 0) return error.MismatchedLoop;
        if (quote != null) return error.MismatchedQuote;
        return .{
            .program = program,
            .delims = delims,
            .literals = literals,
            .input = input,
            .output = output,
            .allocator = allocator,
        };
    }

    pub fn deinit(int: *Interpreter) void {
        int.delims.deinit(int.allocator);
        int.literals.deinit(int.allocator);
        for (&int.stacks) |*stack| {
            stack.deinit(int.allocator);
        }
        int.* = undefined;
    }

    pub const StepError = error{
        OutOfMemory,
        Halt,
        Input,
        Output,
        StackEmpty,
        DivisionByZero,
        InvalidInstruction,
    };

    pub fn step(int: *Interpreter) StepError!void {
        while (int.pc < int.program.len and std.ascii.isWhitespace(int.program[int.pc])) int.pc += 1;
        if (int.pc == int.program.len) return error.Halt;
        switch (int.program[int.pc]) {
            '0'...'9' => |d| try int.stacks[int.stack].append(int.allocator, d - '0'),
            'a'...'z', 'A'...'Z' => |c| try int.stacks[int.stack].append(int.allocator, c),
            '+' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b +% a);
            },
            '-' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b -% a);
            },
            '*' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b *% a);
            },
            '/' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                if (a == 0) return error.DivisionByZero;
                int.stacks[int.stack].appendAssumeCapacity(b / a);
            },
            '%' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                if (a == 0) return error.DivisionByZero;
                int.stacks[int.stack].appendAssumeCapacity(b % a);
            },
            '&' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b & a);
            },
            '|' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b | a);
            },
            '^' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(b ^ a);
            },
            '_' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                int.stacks[int.stack].items.len -= 1;
            },
            '#' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                try int.stacks[int.stack].append(int.allocator, int.stacks[int.stack].getLast());
            },
            '@' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(a);
                int.stacks[int.stack].appendAssumeCapacity(b);
            },
            '>' => int.stack +%= 1,
            '<' => int.stack -%= 1,
            '.' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                int.output.writeByte(int.stacks[int.stack].pop()) catch return error.Output;
            },
            ',' => {
                const b = int.input.readByte() catch return error.Input;
                try int.stacks[int.stack].append(int.allocator, b);
            },
            ';' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                int.output.print("{} ", .{int.stacks[int.stack].pop()}) catch return error.Output;
            },
            '[' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                if (int.stacks[int.stack].getLast() == 0) int.pc = int.delims.get(int.pc).?;
            },
            ']' => int.pc = int.delims.get(int.pc).? - 1,
            '{' => if (int.stacks[int.stack].items.len == 0) {
                int.pc = int.delims.get(int.pc).?;
            },
            '}' => int.pc = int.delims.get(int.pc).? - 1,
            '=' => {
                if (int.stacks[int.stack].items.len < 2) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                const b = int.stacks[int.stack].pop();
                int.registers[b] = a;
            },
            '$' => {
                if (int.stacks[int.stack].items.len < 1) return error.StackEmpty;
                const a = int.stacks[int.stack].pop();
                int.stacks[int.stack].appendAssumeCapacity(int.registers[a]);
            },
            '"' => {
                if (int.delims.get(int.pc)) |str_end| {
                    const str = int.program[int.pc + 1 .. str_end];
                    try int.stacks[int.stack].ensureUnusedCapacity(int.allocator, str.len);
                    var i: usize = str.len;
                    while (i > 0) {
                        i -= 1;
                        int.stacks[int.stack].appendAssumeCapacity(str[i]);
                    }
                    int.pc = str_end;
                }
            },
            '\'' => {
                if (int.delims.get(int.pc)) |str_end| {
                    try int.stacks[int.stack].append(int.allocator, int.literals.get(int.pc).?);
                    int.pc = str_end;
                }
            },
            else => return error.InvalidInstruction,
        }
        int.pc += 1;
    }
};

const log = std.log.scoped(.minim);

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    var eof: u8 = 0;
    var interactive = false;
    var input: union(enum) { none, stdin, file: []const u8 } = .none;
    // TODO: standard argument parsing (e.g. --, -e255, etc.)
    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const arg = args[i];
        if (std.mem.eql(u8, arg, "-e") or std.mem.eql(u8, arg, "--eof")) {
            i += 1;
            if (i == args.len) fatal("missing argument to -e/--eof", .{});
            eof = std.fmt.parseUnsigned(u8, args[i], 10) catch fatal("invalid argument to -e/--eof", .{});
        } else if (std.mem.eql(u8, arg, "-i") or std.mem.eql(u8, arg, "--interactive")) {
            interactive = true;
        } else if (std.mem.eql(u8, arg, "-")) {
            if (input != .none) fatal("too many input files", .{});
            input = .stdin;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            fatal("unrecognized option: {s}", .{arg});
        } else {
            if (input != .none) fatal("too many input files", .{});
            input = .{ .file = arg };
        }
    }

    if (interactive) fatal("TODO: interactive mode", .{});

    const program = switch (input) {
        .none, .stdin => try std.io.getStdIn().readToEndAlloc(allocator, std.math.maxInt(usize)),
        .file => |path| program: {
            const file = try std.fs.cwd().openFile(path, .{});
            defer file.close();
            break :program try file.readToEndAlloc(allocator, std.math.maxInt(usize));
        },
    };
    defer allocator.free(program);

    const stdin_reader = std.io.getStdIn().reader();
    const stdout_writer = std.io.getStdOut().writer();
    var int = try Interpreter.init(allocator, program, stdin_reader.any(), stdout_writer.any());
    defer int.deinit();
    while (true) {
        int.step() catch |e| switch (e) {
            error.Halt => break,
            else => {
                std.debug.print("{} '{c}' {}\n", .{ int.pc, int.program[int.pc], e });
                return e;
            },
        };
    }
}

fn fatal(comptime fmt: []const u8, args: anytype) noreturn {
    @setCold(true);
    log.err(fmt, args);
    std.process.exit(1);
}
