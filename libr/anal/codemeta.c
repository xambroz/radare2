/* radare2 - LGPL - Copyright 2020-2021 - nimmumanoj, pancake */

#include <r_core.h>
#include <r_codemeta.h>
#include <r_util.h>

R_API RCodeMeta *r_codemeta_new(const char *code) {
	RCodeMeta *r = R_NEW0 (RCodeMeta);
	if (!r) {
		return NULL;
	}
	r->tree = r_rbtree_cont_new ();
	r->code = code? strdup (code): NULL;
	r_vector_init (&r->annotations, sizeof (RCodeMetaItem), (RVectorFree)r_codemeta_item_fini, NULL);
	return r;
}

R_API RCodeMetaItem *r_codemeta_item_new(void) {
	return R_NEW0 (RCodeMetaItem);
}

R_API void r_codemeta_item_free(RCodeMetaItem *mi) {
	if (mi) {
		r_codemeta_item_fini (mi);
		free (mi);
	}
}

R_API void r_codemeta_item_fini(RCodeMetaItem *mi) {
	r_return_if_fail (mi);
	switch (mi->type) {
	case R_CODEMETA_TYPE_FUNCTION_NAME:
		free (mi->reference.name);
		break;
	case R_CODEMETA_TYPE_LOCAL_VARIABLE:
	case R_CODEMETA_TYPE_FUNCTION_PARAMETER:
		free (mi->variable.name);
		break;
	case R_CODEMETA_TYPE_CONSTANT_VARIABLE:
	case R_CODEMETA_TYPE_OFFSET:
	case R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT:
	case R_CODEMETA_TYPE_GLOBAL_VARIABLE:
		break;
	}
}

R_API bool r_codemeta_item_is_reference(RCodeMetaItem *mi) {
	r_return_val_if_fail (mi, false);
	return (mi->type == R_CODEMETA_TYPE_GLOBAL_VARIABLE || mi->type == R_CODEMETA_TYPE_CONSTANT_VARIABLE || mi->type == R_CODEMETA_TYPE_FUNCTION_NAME);
}

R_API bool r_codemeta_item_is_variable(RCodeMetaItem *mi) {
	r_return_val_if_fail (mi, false);
	return (mi->type == R_CODEMETA_TYPE_LOCAL_VARIABLE || mi->type == R_CODEMETA_TYPE_FUNCTION_PARAMETER);
}

R_API void r_codemeta_free(RCodeMeta *code) {
	if (!code) {
		return;
	}
	r_vector_clear (&code->annotations);
	r_rbtree_cont_free (code->tree);
	r_free (code->code);
	r_free (code);
}

static int cm_add(void *incoming, void *in, void *user) {
	RCodeMetaItem *mi = in;
	RCodeMetaItem *mi2 = incoming;
	return mi2->start - mi->start;
}

#define USE_TRI 0

#if USE_TRI
static int cm_cmp(void *incoming, void *in, void *user) {
	RCodeMetaItem *mi = in;
	RCodeMetaItem *mi2 = incoming;
	ut64 at = mi2->start;
	if (at >= mi->start && at <= mi->end) {
		RPVector *r = (RPVector*)user;
		r_pvector_push (r, mi);
		return 0;
	}
	if (at > mi2->end) {
		return -1;
	}
	return 1;
}
#endif

R_API void r_codemeta_add_annotation(RCodeMeta *code, RCodeMetaItem *mi) {
	r_return_if_fail (code && mi);
	r_vector_push (&code->annotations, mi);
	r_rbtree_cont_insert (code->tree, mi, cm_add, NULL);
}

R_API RPVector *r_codemeta_at(RCodeMeta *code, size_t offset) {
	return r_codemeta_in (code, offset, offset + 1);
}

R_API RPVector *r_codemeta_in(RCodeMeta *code, size_t start, size_t end) {
	r_return_val_if_fail (code, NULL);
	RPVector *r = r_pvector_new (NULL);
	if (!r) {
		return NULL;
	}
#if USE_TRI
	RCodeMetaItem my = { .start = start, .end = end };
	r_rbtree_cont_find (code->tree, &my, cm_cmp, r);
	return r;
#else
	RCodeMetaItem *mi;
	r_vector_foreach (&code->annotations, mi) {
		if (start >= mi->end || end < mi->start) {
			continue;
		}
		r_pvector_push (r, mi);
	}
	return r;
#endif
}

R_API RVector *r_codemeta_line_offsets(RCodeMeta *code) {
	r_return_val_if_fail (code, NULL);
	RVector *r = r_vector_new (sizeof (ut64), NULL, NULL);
	if (!r) {
		return NULL;
	}
	size_t cur = 0;
	size_t len = strlen (code->code);
	do {
		char *next = strchr (code->code + cur, '\n');
		size_t next_i = next? (next - code->code) + 1: len;
		RPVector *annotations = r_codemeta_in (code, cur, next_i);
		ut64 offset = UT64_MAX;
		void **it;
		r_pvector_foreach (annotations, it) {
			RCodeMetaItem *mi = *it;
			if (mi->type != R_CODEMETA_TYPE_OFFSET) {
				continue;
			}
			offset = mi->offset.offset;
			break;
		}
		r_vector_push (r, &offset);
		cur = next_i;
		r_pvector_free (annotations);
	} while (cur < len);
	return r;
}

// print methods
R_API void r_codemeta_print_json(RCodeMeta *code) {
	PJ *pj = pj_new ();
	if (!pj) {
		return;
	}

	pj_o (pj);
	pj_ks (pj, "code", code->code);

	pj_k (pj, "annotations");
	pj_a (pj);

	char *type_str;
	RCodeMetaItem *annotation;
	r_vector_foreach (&code->annotations, annotation) {
		pj_o (pj);
		pj_kn (pj, "start", (ut64)annotation->start);
		pj_kn (pj, "end", (ut64)annotation->end);
		switch (annotation->type) {
		case R_CODEMETA_TYPE_OFFSET:
			pj_ks (pj, "type", "offset");
			pj_kn (pj, "offset", annotation->offset.offset);
			break;
		case R_CODEMETA_TYPE_FUNCTION_NAME:
			pj_ks (pj, "type", "function_name");
			pj_ks (pj, "name", annotation->reference.name);
			pj_kn (pj, "offset", annotation->reference.offset);
			break;
		case R_CODEMETA_TYPE_GLOBAL_VARIABLE:
			pj_ks (pj, "type", "global_variable");
			pj_kn (pj, "offset", annotation->reference.offset);
			break;
		case R_CODEMETA_TYPE_CONSTANT_VARIABLE:
			pj_ks (pj, "type", "constant_variable");
			pj_kn (pj, "offset", annotation->reference.offset);
			break;
		case R_CODEMETA_TYPE_LOCAL_VARIABLE:
			pj_ks (pj, "type", "local_variable");
			pj_ks (pj, "name", annotation->variable.name);
			break;
		case R_CODEMETA_TYPE_FUNCTION_PARAMETER:
			pj_ks (pj, "type", "function_parameter");
			pj_ks (pj, "name", annotation->variable.name);
			break;
		case R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT:
			pj_ks (pj, "type", "syntax_highlight");
			type_str = NULL;
			switch (annotation->syntax_highlight.type) {
			case R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD:
				type_str = "keyword";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_COMMENT:
				type_str = "comment";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_DATATYPE:
				type_str = "datatype";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME:
				type_str = "function_name";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_PARAMETER:
				type_str = "function_parameter";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_LOCAL_VARIABLE:
				type_str = "local_variable";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_CONSTANT_VARIABLE:
				type_str = "constant_variable";
				break;
			case R_SYNTAX_HIGHLIGHT_TYPE_GLOBAL_VARIABLE:
				type_str = "global_variable";
				break;
			}
			if (type_str) {
				pj_ks (pj, "syntax_highlight", type_str);
			}
			break;
		}
		pj_end (pj);
	}
	pj_end (pj);

	pj_end (pj);
	r_cons_printf ("%s\n", pj_string (pj));
	pj_free (pj);
}

#define PALETTE(x) (cons && cons->context->pal.x) ? cons->context->pal.x
#define PRINT_COLOR(x)                             \
	do {                                       \
		if (cons->context->color_mode) {   \
			r_cons_printf ("%s", (x)); \
		}                                  \
	} while (0)

/**
 * @param width maximum nibbles per address
 */
static void print_offset_in_binary_line_bar(RCodeMeta *code, ut64 offset, size_t width) {
	static const char *fmt[9] = {
		"0x%08" PFMT64x,
		"0x%09" PFMT64x,
		"0x%010" PFMT64x,
		"0x%011" PFMT64x,
		"0x%012" PFMT64x,
		"0x%013" PFMT64x,
		"0x%014" PFMT64x,
		"0x%015" PFMT64x,
		"0x%016" PFMT64x
	};
	if (width < 8) {
		width = 8;
	}
	if (width > 16) {
		width = 16;
	}
	width -= 8;

	RCons *cons = r_cons_singleton ();
	r_cons_printf ("    ");
	if (offset == UT64_MAX) {
		r_cons_print ("          ");
		while (width > 0) {
			r_cons_print (" ");
			width--;
		}
	} else {
		PRINT_COLOR (PALETTE (offset)
			     : Color_GREEN);
		r_cons_printf (fmt[width], offset);
		PRINT_COLOR (Color_RESET);
	}
	r_cons_printf ("    |");
}

R_API void r_codemeta_print(RCodeMeta *code, RVector *line_offsets) {
	if (code->annotations.len == 0) {
		r_cons_printf ("%s\n", code->code);
		return;
	}

	size_t cur = 0;
	size_t line_idx = 0;
	size_t len = strlen (code->code);

	size_t offset_width = 0;
	if (line_offsets) {
		ut64 *offset;
		ut64 offset_max = 0;
		r_vector_foreach (line_offsets, offset) {
			if (*offset != UT64_MAX && *offset > offset_max) {
				offset_max = *offset;
			}
		}
		while (offset_max) {
			offset_width += 1;
			offset_max >>= 4;
		}
		if (offset_width < 4) {
			offset_width = 4;
		}
	}

	RCons *cons = r_cons_singleton ();
	RCodeMetaItem *annotation;
	r_vector_foreach (&code->annotations, annotation) {
		if (annotation->type != R_CODEMETA_TYPE_SYNTAX_HIGHLIGHT) {
			continue;
		}

		// (1/3)
		// now we have a syntax highlighting annotation.
		// pick a suitable color for it.
		const char *color = Color_RESET;
		switch (annotation->syntax_highlight.type) {
		case R_SYNTAX_HIGHLIGHT_TYPE_COMMENT:
			color = PALETTE (comment)
			    : Color_WHITE;
			break;
		case R_SYNTAX_HIGHLIGHT_TYPE_KEYWORD:
			color = PALETTE (pop)
			    : Color_MAGENTA;
			break;
		case R_SYNTAX_HIGHLIGHT_TYPE_DATATYPE:
			color = PALETTE (func_var_type)
			    : Color_BLUE;
			break;
		case R_SYNTAX_HIGHLIGHT_TYPE_FUNCTION_NAME:
			color = PALETTE (fname)
			    : Color_RED;
			break;
		case R_SYNTAX_HIGHLIGHT_TYPE_CONSTANT_VARIABLE:
			color = PALETTE (num)
			    : Color_YELLOW;
		default:
			break;
		}

		// (2/3)
		// the chunk before the syntax highlighting annotation should not be colored
		for (; cur < annotation->start && cur < len; cur++) {
			// if we are starting a new line and we are printing with offsets
			// we need to prepare the bar with offsets on the left handside before that
			if (line_offsets && (cur == 0 || code->code[cur - 1] == '\n')) {
				ut64 offset = 0;
				if (line_idx < line_offsets->len) {
					offset = *(ut64 *)r_vector_index_ptr (line_offsets, line_idx);
				}
				print_offset_in_binary_line_bar (code, offset, offset_width);
				line_idx++;
			}
			r_cons_printf ("%c", code->code[cur]);
		}

		// (3/3)
		// everything in between the "start" and the "end" inclusive should be highlighted
		PRINT_COLOR (color);
		for (; cur < annotation->end && cur < len; cur++) {
			// if we are starting a new line and we are printing with offsets
			// we need to prepare the bar with offsets on the left handside before that
			if (line_offsets && (cur == 0 || code->code[cur - 1] == '\n')) {
				ut64 offset = 0;
				if (line_idx < line_offsets->len) {
					offset = *(ut64 *)r_vector_index_ptr (line_offsets, line_idx);
				}
				PRINT_COLOR (Color_RESET);
				print_offset_in_binary_line_bar (code, offset, offset_width);
				PRINT_COLOR (color);
				line_idx++;
			}
			r_cons_printf ("%c", code->code[cur]);
		}
		PRINT_COLOR (Color_RESET);
	}
	// the rest of the decompiled code should be printed
	// without any highlighting since we don't have any annotations left
	for (; cur < len; cur++) {
		// if we are starting a new line and we are printing with offsets
		// we need to prepare the bar with offsets on the left handside before that
		if (line_offsets && (cur == 0 || code->code[cur - 1] == '\n')) {
			ut64 offset = 0;
			if (line_idx < line_offsets->len) {
				offset = *(ut64 *)r_vector_index_ptr (line_offsets, line_idx);
			}
			print_offset_in_binary_line_bar (code, offset, offset_width);
			line_idx++;
		}
		r_cons_printf ("%c", code->code[cur]);
	}
}

static bool foreach_offset_annotation(void *user, const ut64 offset, const void *val) {
	RCodeMeta *code = user;
	const RCodeMetaItem *annotation = val;
	char *b64statement = r_base64_encode_dyn (code->code + annotation->start, annotation->end - annotation->start);
	r_cons_printf ("CCu base64:%s @ 0x%" PFMT64x "\n", b64statement, annotation->offset.offset);
	free (b64statement);
	return true;
}

R_API void r_codemeta_print_comment_cmds(RCodeMeta *code) {
	RCodeMetaItem *annotation;
	HtUP *ht = ht_up_new0 ();
	r_vector_foreach (&code->annotations, annotation) {
		if (annotation->type != R_CODEMETA_TYPE_OFFSET) {
			continue;
		}
		// choose the "best" annotation at a single offset
		RCodeMetaItem *prev_annot = ht_up_find (ht, annotation->offset.offset, NULL);
		if (prev_annot) {
			if (annotation->end - annotation->start < prev_annot->end - prev_annot->start) {
				continue;
			}
		}
		ht_up_update (ht, annotation->offset.offset, annotation);
	}
	ht_up_foreach (ht, foreach_offset_annotation, code);
	ht_up_free (ht);
}
