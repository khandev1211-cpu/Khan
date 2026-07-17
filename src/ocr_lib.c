#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tesseract/capi.h>
#include "vision_lib.h"
#include "ocr_lib.h"

/* ===========================================================================
 * Engine slots
 *
 * Same pattern as vision_cascade.c's HaarCascade table: a small fixed-size
 * array of native handles, addressed from Khan by index. Each Khan-visible
 * "engine" map just carries its slot number back to us.
 * ========================================================================= */

#define OCR_MAX_HANDLES 8

static TessBaseAPI *g_ocr_handles[OCR_MAX_HANDLES];
static int          g_ocr_last_conf[OCR_MAX_HANDLES];

static int ocr_alloc_slot(void) {
    for (int i = 0; i < OCR_MAX_HANDLES; i++) if (!g_ocr_handles[i]) return i;
    return -1;
}

/* Resolve a Khan-level engine map back to its native slot, or -1 if the
 * argument isn't a live engine (wrong type, freed already, corrupt id). */
static int ocr_resolve_slot(Value *engine) {
    if (engine->type != VAL_MAP) return -1;
    Value *idv = map_get(engine, "__ocr_id");
    if (!idv || idv->type != VAL_NUMBER) return -1;
    int slot = (int)idv->as.number;
    if (slot < 0 || slot >= OCR_MAX_HANDLES || !g_ocr_handles[slot]) return -1;
    return slot;
}

/* ===========================================================================
 * Native functions
 * ========================================================================= */

void fn_ocr_init(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();

    const char *lang = "eng";
    if (argc >= 1) {
        if (args[0].type != VAL_STRING) return;
        lang = args[0].as.string;
    }
    const char *datapath = NULL;
    if (argc >= 2) {
        if (args[1].type != VAL_STRING) return;
        datapath = args[1].as.string;
    }

    int slot = ocr_alloc_slot();
    if (slot < 0) {
        fprintf(stderr,
            "Runtime error: ocr_init() - too many open OCR engines (max %d); "
            "call ocr_free() on ones you're done with\n", OCR_MAX_HANDLES);
        return;
    }

    TessBaseAPI *handle = TessBaseAPICreate();
    if (!handle) return;

    if (TessBaseAPIInit3(handle, datapath, lang) != 0) {
        fprintf(stderr,
            "Runtime error: ocr_init(\"%s\") failed - couldn't find trained "
            "language data. Pass an explicit datapath as the 2nd argument, "
            "or install the language pack (e.g. tesseract-ocr-%s). "
            "See docs/ocr.md.\n", lang, lang);
        TessBaseAPIDelete(handle);
        return;
    }

    g_ocr_handles[slot]   = handle;
    g_ocr_last_conf[slot] = -1;

    Value m = value_map_empty();
    map_set(&m, "__ocr_id", value_number(slot));
    map_set(&m, "lang", value_string(lang));
    *result = m;
}

void fn_ocr_recognize(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 2) return;

    int slot = ocr_resolve_slot(&args[0]);
    if (slot < 0) return;

    int img_slot = vision_internal_get_slot(args[1]);
    if (img_slot < 0) return;

    unsigned char *data = vision_internal_data(img_slot);
    int w = vision_internal_width(img_slot);
    int h = vision_internal_height(img_slot);
    int c = vision_internal_channels(img_slot);
    if (!data || w <= 0 || h <= 0) return;
    if (c != 1 && c != 3 && c != 4) {
        fprintf(stderr,
            "Runtime error: ocr_recognize() - unsupported channel count %d "
            "(need 1 [grayscale], 3 [RGB], or 4 [RGBA]); try "
            "vision_grayscale(image) first\n", c);
        return;
    }

    TessBaseAPI *handle = g_ocr_handles[slot];
    TessBaseAPISetImage(handle, data, w, h, c, w * c);

    char *text = TessBaseAPIGetUTF8Text(handle);
    g_ocr_last_conf[slot] = TessBaseAPIMeanTextConf(handle);
    if (!text) return;

    *result = value_string(text);
    TessDeleteText(text);
}

void fn_ocr_confidence(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1) return;

    int slot = ocr_resolve_slot(&args[0]);
    if (slot < 0) return;
    if (g_ocr_last_conf[slot] < 0) return; /* recognize() hasn't run yet */
    *result = value_number(g_ocr_last_conf[slot]);
}

void fn_ocr_set_page_seg_mode(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 2 || args[1].type != VAL_NUMBER) return;

    int slot = ocr_resolve_slot(&args[0]);
    if (slot < 0) return;

    int mode = (int)args[1].as.number;
    if (mode < 0 || mode > 13) return; /* PSM_OSD_ONLY..PSM_COUNT-1 */
    TessBaseAPISetPageSegMode(g_ocr_handles[slot], (TessPageSegMode)mode);
}

void fn_ocr_free(Value *result, Interpreter *interp, int argc, Value *args) {
    (void)interp;
    *result = value_nil();
    if (argc < 1) return;

    int slot = ocr_resolve_slot(&args[0]);
    if (slot < 0) return;

    TessBaseAPIEnd(g_ocr_handles[slot]);
    TessBaseAPIDelete(g_ocr_handles[slot]);
    g_ocr_handles[slot]   = NULL;
    g_ocr_last_conf[slot] = -1;
}

/* ===========================================================================
 * Registration
 * ========================================================================= */

void ocr_register_all(Environment *env) {
    env_define(env, "ocr_init",              value_native("ocr_init",              fn_ocr_init));
    env_define(env, "ocr_recognize",         value_native("ocr_recognize",         fn_ocr_recognize));
    env_define(env, "ocr_confidence",        value_native("ocr_confidence",        fn_ocr_confidence));
    env_define(env, "ocr_set_page_seg_mode", value_native("ocr_set_page_seg_mode", fn_ocr_set_page_seg_mode));
    env_define(env, "ocr_free",              value_native("ocr_free",              fn_ocr_free));
}

void ocr_register_all_vm(VM *vm) {
    vm_global_set_native(vm, "ocr_init",              fn_ocr_init);
    vm_global_set_native(vm, "ocr_recognize",         fn_ocr_recognize);
    vm_global_set_native(vm, "ocr_confidence",        fn_ocr_confidence);
    vm_global_set_native(vm, "ocr_set_page_seg_mode", fn_ocr_set_page_seg_mode);
    vm_global_set_native(vm, "ocr_free",              fn_ocr_free);
}
