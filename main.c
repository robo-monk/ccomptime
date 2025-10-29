#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#include "comptime_common.h"
#include "macro_expansion.h"
#include "tree_passes.h"

#include "cli.c"

#define TS_INCLUDE_SYMBOLS
#include "tree_sitter_c_api.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const TSLanguage *tree_sitter_c(void);

static void build_runner_snippets(WalkContext *ctx,
                                  String_Builder *runner_definitions,
                                  String_Builder *runner_main) {
  int comptime_count = 0;

  nob_da_foreach(Slice, it, &ctx->comptime_stmts) {
    nob_sb_appendf(runner_definitions, "\n__Comptime_Statement_Fn(%d, %.*s)\n",
                   comptime_count, it->len, it->start);

    nob_sb_appendf(runner_main,
                   "__Comptime_Register_Main_Exec(%d); // "
                   "execute comptime statement #%d\n",
                   comptime_count, comptime_count);

    comptime_count++;
  }

  ctx->comptime_count = comptime_count;
}

static void build_compile_base_command(Nob_Cmd *out, CliArgs *parsed_argv) {
  nob_cmd_append(out, Parsed_Argv_compiler_name(parsed_argv));
  cmd_append_arg_indeces(parsed_argv, &parsed_argv->flags, out);

  if (!(parsed_argv->cct_flags & CliComptimeFlag_Debug)) {
    nob_cmd_append(out, "-w");
  } else {
    nob_cmd_append(out, "-g", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer");
  }
}

static void build_stripped_source(WalkContext *ctx,
                                  const char *processed_source, size_t len,
                                  String_Builder *out) {
  const char *cursor = processed_source;

  nob_da_foreach(Slice, it, &ctx->to_be_removed) {
    if (cursor >= it->start)
      continue;

    assert(cursor < it->start && "cursor is ahead of comptime slice");

    ssize_t offset = it->start - cursor;
    assert(offset >= 0);

    if (offset > 0) {
      nob_sb_append_buf(out, cursor, (size_t)offset);
    }

    cursor = it->start + it->len;
  }

  ssize_t tail = processed_source + len - cursor;
  assert(tail >= 0);
  nob_sb_append_buf(out, cursor, (size_t)tail);
}

static void build_header_prelude(WalkContext *ctx) {
  sb_appendf(&ctx->out_h, "/*// @generated - ccomptime™ v0.0.1 - %lu \\*/\n",
             time(NULL));
  sb_appendf(
      &ctx->out_h,
      "#pragma once\n"
      "#define _CONCAT_(x, y) x##y\n"
      "#define CONCAT(x, y) _CONCAT_(x, y)\n"
      "#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)\n");
  sb_appendf(&ctx->out_h, "/* ---- */// the end. ///* ---- */\n");
}

static void write_final_wrapper(const Context *ctx) {
  String_Builder final_source = {0};
  const char *header_basename = path_basename(ctx->gen_header_path);
  const char *input_basename = path_basename(ctx->input_path);

  sb_appendf(&final_source, "#include \"%s\"\n", header_basename);
  sb_appendf(&final_source, "#include \"%s\"\n", input_basename);

  nob_write_entire_file(ctx->final_out_path, final_source.items,
                        final_source.count);

  sb_free(final_source);
}

static void run_file(Context *ctx) {
  size_t mark = nob_temp_save();

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_c());

  TSTree *raw_tree = ts_parser_parse_string(
      parser, NULL, ctx->raw_source->items, ctx->raw_source->count);

  debug_tree(raw_tree, ctx->raw_source->items, 0);

  String_Builder pp_source = {0};
  MacroDefinitionHashMap macros = {0};
  TSTree *pp_tree =
      cct_expand_macros(parser, raw_tree, &macros, ctx->raw_source->items,
                        ctx->raw_source->count, &pp_source);

  String_Builder processed_source = {0};
  WalkContext walk_ctx = {0};
  TSTree *clean_tree = cct_correct_comptimetype_nodes(
      parser, pp_tree, pp_source.items, pp_source.count, &walk_ctx,
      &processed_source);

  cct_collect_comptime_statements(&walk_ctx, clean_tree,
                                  processed_source.items);

  String_Builder runner_definitions = {0};
  String_Builder runner_main = {0};
  build_runner_snippets(&walk_ctx, &runner_definitions, &runner_main);

  String_Builder stripped_input_source = {0};
  build_stripped_source(&walk_ctx, processed_source.items,
                        processed_source.count, &stripped_input_source);

  build_header_prelude(&walk_ctx);

  nob_write_entire_file(ctx->stripped_source_path, stripped_input_source.items,
                        stripped_input_source.count);

  nob_write_entire_file(ctx->runner_defs_path, runner_definitions.items,
                        runner_definitions.count);

  nob_write_entire_file(ctx->runner_main_path, runner_main.items,
                        runner_main.count);

  nob_write_entire_file(ctx->gen_header_path, walk_ctx.out_h.items,
                        walk_ctx.out_h.count);

  Nob_Cmd build_cmd = {0};
  build_compile_base_command(&build_cmd, ctx->parsed_argv);

  nob_cmd_append(&build_cmd, resolve(__FILE__, "runner.templ.c"));
  nob_cmd_append(&build_cmd, "-o", ctx->runner_exepath);
  nob_cmd_append(
      &build_cmd,
      temp_sprintf("-D_INPUT_PROGRAM_PATH=\"%s\"", ctx->stripped_source_path),
      temp_sprintf("-D_INPUT_COMPTIME_DEFS_PATH=\"%s\"", ctx->runner_defs_path),
      temp_sprintf("-D_INPUT_COMPTIME_MAIN_PATH=\"%s\"", ctx->runner_main_path),
      temp_sprintf("-D_OUTPUT_HEADERS_PATH=\"%s\"", ctx->gen_header_path));

  if (!nob_cmd_run(&build_cmd))
    fatal("Failed to compile comptime runner");

  ts_tree_delete(clean_tree);
  ts_parser_delete(parser);

  // TODO: this is bad structure, bad ownership
  sb_free(pp_source);
  sb_free(processed_source);

  sb_free(runner_definitions);
  sb_free(runner_main);
  sb_free(stripped_input_source);
  sb_free(walk_ctx.out_h);

  nob_temp_rewind(mark);
}

int main(int argc, char **argv) {
  CliArgs parsed_argv = {0};
  if (cli(argc, argv, &parsed_argv) != 0) {
    return 1;
  }

  nob_log(INFO, "Received %d arguments", argc);
  nob_log(INFO, "Using compiler %s", Parsed_Argv_compiler_name(&parsed_argv));

  struct {
    const char **items;
    size_t count, capacity;
  } files_to_remove = {0};

  nob_da_foreach(int, index, &parsed_argv.input_files) {
    nob_log(INFO, "Processing input file %s", argv[*index]);

    const char *input_filename = argv[*index];
    String_Builder raw_source = {0};
    String_Builder preprocessed_source = {0};
    Context ctx = {0};
    ctx.input_path = input_filename;
    ctx.parsed_argv = &parsed_argv;
    ctx.raw_source = &raw_source;
    ctx.preprocessed_source = &preprocessed_source;

    Context_fill_paths(&ctx, input_filename);

    nob_log(VERBOSE, "Processing %s source as SourceCode", input_filename);
    nob_read_entire_file(input_filename, ctx.raw_source);

    run_file(&ctx);

    // sb_free(raw_source);
    // sb_free(preprocessed_source);

    Nob_Cmd cmd = {0};
    nob_log(INFO, "Running runner %s", ctx.runner_exepath);
    printf("=== Compile time logs ===\n");
    nob_cmd_append(&cmd, nob_temp_sprintf("./%s", ctx.runner_exepath));
    if (!nob_cmd_run(&cmd)) {
      nob_log(ERROR, "failed to run runner %s", ctx.runner_templ_path);
      exit(1);
    }
    fflush(stdout);
    printf("=== end === \n");

    write_final_wrapper(&ctx);
    parsed_argv.argv[*index] = (char *)ctx.final_out_path;

    da_append(&files_to_remove, ctx.preprocessed_path);
    da_append(&files_to_remove, ctx.runner_main_path);
    da_append(&files_to_remove, ctx.runner_defs_path);
    da_append(&files_to_remove, ctx.stripped_source_path);
    da_append(&files_to_remove, ctx.runner_templ_path);
    da_append(&files_to_remove, ctx.runner_exepath);
    da_append(&files_to_remove, ctx.vals_path);
    da_append(&files_to_remove, ctx.final_out_path);
  }

  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.input_files, &final);
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.output_files, &final);
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.flags, &final);

  if (!cmd_run(&final)) {
    nob_log(ERROR, "failed to compile final output");
    return 1;
  } else {
    nob_log(INFO, "Successfully compiled final output");
  }

  nob_log(INFO, "Cleaning up %zu intermediate files", files_to_remove.count);
  nob_da_foreach(const char *, f, &files_to_remove) {
    if (!(parsed_argv.cct_flags & CliComptimeFlag_KeepInter)) {
      nob_log(NOB_WARNING, "Deleting intermediate file %s", *f);
      delete_file(*f);
    } else {
      nob_log(INFO, "Keeping intermediate file %s", *f);
    }
  }
  return 0;
}
