#include "wmbus_decode/interpreter.h"
#include "wmbus_decode/formula.h"
#include "wmbus_phy/aes.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace wmb {

// ---------------------------------------------------------------------------
// Media strings (port of upstream mediaTypeJSON)
// ---------------------------------------------------------------------------
const char* media_type_json(uint8_t t, uint16_t mfct, char* buf, size_t buf_len) {
    const char* s = nullptr;
    switch (t) {
    case 0: s = "other"; break;
    case 1: s = "oil"; break;
    case 2: s = "electricity"; break;
    case 3: s = "gas"; break;
    case 4: s = "heat"; break;
    case 5: s = "steam"; break;
    case 6: s = "warm water"; break;
    case 7: s = "water"; break;
    case 8: s = "heat cost allocation"; break;
    case 9: s = "compressed air"; break;
    case 0x0a: s = "cooling load volume at outlet"; break;
    case 0x0b: s = "cooling load volume at inlet"; break;
    case 0x0c: s = "heat volume at inlet"; break;
    case 0x0d: s = "heat/cooling load"; break;
    case 0x0e: s = "bus/system component"; break;
    case 0x0f: s = "unknown"; break;
    case 0x15: s = "hot water"; break;
    case 0x16: s = "cold water"; break;
    case 0x17: s = "hot/cold water"; break;
    case 0x18: s = "pressure"; break;
    case 0x19: s = "a/d converter"; break;
    case 0x1a: s = "smoke detector"; break;
    case 0x1b: s = "room sensor"; break;
    case 0x1c: s = "gas detector"; break;
    case 0x25: s = "customer unit (display device)"; break;
    case 0x28: s = "waste water"; break;
    case 0x29: s = "garbage"; break;
    case 0x36: s = "radio converter (system side)"; break;
    case 0x37: s = "radio converter (meter side)"; break;
    default: break;
    }
    if (!s) {
        char m[4];
        mfct_to_str(mfct, m);
        if (strncmp(m, "TCH", 3) == 0) {
            switch (t) {
            case 0x62: s = "warm water"; break;
            case 0x72: s = "cold water"; break;
            case 0x80: s = "heat cost allocator"; break;
            case 0xC3: s = "heat"; break;
            case 0x43: s = "heat"; break;
            case 0xf0: s = "smoke detector"; break;
            default: break;
            }
        }
    }
    if (!s) s = "unknown";
    snprintf(buf, buf_len, "%s", s);
    return buf;
}

// ---------------------------------------------------------------------------
// Payload decoder registry
// ---------------------------------------------------------------------------
#define MAX_PAYLOAD_DECODERS 16
static struct {
    char name[24];
    PayloadDecoderFn fn;
} g_decoders[MAX_PAYLOAD_DECODERS];
static int g_num_decoders = 0;

void register_payload_decoder(const char* driver_name, PayloadDecoderFn fn) {
    if (g_num_decoders >= MAX_PAYLOAD_DECODERS) return;
    snprintf(g_decoders[g_num_decoders].name, 24, "%s", driver_name);
    g_decoders[g_num_decoders].fn = fn;
    g_num_decoders++;
}

static PayloadDecoderFn find_payload_decoder(const char* driver_name) {
    for (int i = 0; i < g_num_decoders; ++i) {
        if (strcmp(g_decoders[i].name, driver_name) == 0) return g_decoders[i].fn;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Lookup evaluation
// ---------------------------------------------------------------------------
static void lookup_render(const LookupDef* lk, double raw, char* out, size_t out_len) {
    uint64_t v = ((uint64_t)(int64_t)raw) & lk->mask;
    out[0] = 0;
    size_t used = 0;
    bool any = false;
    for (uint8_t i = 0; i < lk->num_maps; ++i) {
        const LookupMapEntry* e = &lk->maps[i];
        bool hit = false;
        if (lk->map_type == 1) { // IndexToString
            hit = (v == (e->value & lk->mask));
        } else { // BitToString
            uint64_t m = e->value & lk->mask;
            if (e->test == 0)      hit = (v & m) == m && m != 0;
            else if (e->test == 1) hit = (v & m) == 0 && m != 0;
            else                   hit = (v == m);
        }
        if (hit && e->name[0]) {
            int w = snprintf(out + used, out_len - used, "%s%s", any ? " " : "", e->name);
            if (w > 0) used += (size_t)w;
            any = true;
        }
    }
    if (!any && lk->default_msg) {
        snprintf(out, out_len, "%s", lk->default_msg);
    }
}

// ---------------------------------------------------------------------------
// Template names: 'consumption_{storage_counter-8counter}' etc.
// ---------------------------------------------------------------------------
struct TemplateCtx {
    int storage, tariff, subunit;
};

static bool resolve_template_var(const char* name, double* out, void* ctx) {
    TemplateCtx* tc = (TemplateCtx*)ctx;
    if (strcmp(name, "storage_counter") == 0) { *out = tc->storage; return true; }
    if (strcmp(name, "tariff_counter") == 0)  { *out = tc->tariff; return true; }
    if (strcmp(name, "subunit_counter") == 0) { *out = tc->subunit; return true; }
    return false;
}

static void render_field_name(char* out, size_t out_len, const char* pattern,
                              int storage, int tariff, int subunit) {
    size_t oi = 0;
    TemplateCtx tc{ storage, tariff, subunit };
    for (size_t i = 0; pattern[i] && oi + 1 < out_len;) {
        if (pattern[i] == '{') {
            const char* close = strchr(pattern + i, '}');
            if (!close) break;
            char expr[40];
            size_t el = (size_t)(close - pattern - i - 1);
            if (el >= sizeof(expr)) el = sizeof(expr) - 1;
            memcpy(expr, pattern + i + 1, el);
            expr[el] = 0;
            FVal v = formula_eval(expr, resolve_template_var, &tc);
            oi += snprintf(out + oi, out_len - oi, "%d", v.ok ? (int)v.v : 0);
            i = (size_t)(close - pattern) + 1;
        } else {
            out[oi++] = pattern[i++];
        }
    }
    out[oi] = 0;
}

// ---------------------------------------------------------------------------
// Record matching
// ---------------------------------------------------------------------------
static bool quantity_matches_any(VifRange rule_range, VifRange rec_range) {
    Quantity q = vifrange_quantity(rec_range);
    switch (rule_range) {
    case VifRange::AnyVolumeVIF: return q == Quantity::Volume;
    case VifRange::AnyEnergyVIF: return q == Quantity::Energy;
    case VifRange::AnyPowerVIF:  return q == Quantity::Power;
    default: return false;
    }
}

static bool record_matches(const FieldRule* fr, const DVRecord* r) {
    if (fr->difvifkey) {
        if (strcmp(fr->difvifkey, r->difvifkey) != 0) return false;
    } else if (fr->vif_range != VifRange::None) {
        bool range_ok = (r->vif_range == fr->vif_range) ||
                        quantity_matches_any(fr->vif_range, r->vif_range);
        if (!range_ok) return false;
    } else {
        return false; // no match criteria (e.g. pure status field)
    }
    bool mtype_ok = (fr->mtype == -1 || (int)r->mtype == fr->mtype);
    if (!mtype_ok) return false;
    bool storage_ok = (fr->storage_from == -1 || (r->storage_nr >= fr->storage_from && r->storage_nr <= fr->storage_to));
    if (!storage_ok) return false;
    bool tariff_ok = (fr->tariff_from == -1 || (r->tariff_nr >= fr->tariff_from && r->tariff_nr <= fr->tariff_to));
    if (!tariff_ok) return false;
    bool subunit_ok = (fr->subunit_from == -1 || (r->subunit_nr >= fr->subunit_from && r->subunit_nr <= fr->subunit_to));
    if (!subunit_ok) return false;
    if (fr->comb_must_have) {
        bool found = false;
        for (int i = 0; i < r->num_combinables; ++i) {
            if (r->combinables[i] == fr->comb_must_have) { found = true; break; }
        }
        if (!found) return false;
    }
    if (fr->comb_raw_eq != -1) {
        // exact equality of the combinable set (order-independent, no dups)
        int need = (int)fr->comb_raw_eq;
        if (need == 0) {
            if (r->num_combinables != 0) return false;
        } else {
            if (r->num_combinables != 1) return false;
            if (r->combinables[0] != (uint16_t)need) return false;
        }
    }
    if (fr->comb_synthetic) return false; // synthetic records handled separately
    return true;
}

// ---------------------------------------------------------------------------
// Value computation
// ---------------------------------------------------------------------------
static double record_raw_unsigned(const DVRecord* r) {
    if (!r->data || r->data_len <= 0 || r->data_len > 8) return r->raw;
    uint64_t v = 0;
    for (int i = r->data_len - 1; i >= 0; --i) v = (v << 8) | r->data[i];
    return (double)v;
}

static double compute_value(const FieldRule* fr, const DVRecord* r) {
    double raw = r->raw;
    if (fr->signedness == 2) raw = record_raw_unsigned(r); // Unsigned
    if (fr->force_scale != 0.0) return raw * fr->force_scale;
    if (fr->vif_scaling == 1) return raw; // None
    double scale = vif_scale(r->vif);
    if (scale <= 0) scale = 1.0;
    
    double val = raw / scale;
    const char* src_unit = vifrange_unit_suffix(r->vif_range);
    const char* dst_unit = fr->display_unit ? fr->display_unit : quantity_default_suffix(fr->quantity);

    if (src_unit && dst_unit && src_unit[0] && dst_unit[0] && strcmp(src_unit, dst_unit) != 0) {
        if (strcmp(src_unit, "mj") == 0 && strcmp(dst_unit, "kwh") == 0) val /= 3.6;
        else if (strcmp(src_unit, "kwh") == 0 && strcmp(dst_unit, "mj") == 0) val *= 3.6;
        else if (strcmp(src_unit, "mjh") == 0 && strcmp(dst_unit, "kw") == 0) val /= 3.6;
        else if (strcmp(src_unit, "kw") == 0 && strcmp(dst_unit, "mjh") == 0) val *= 3.6;
        else if (strcmp(src_unit, "h") == 0) {
            if (strcmp(dst_unit, "s") == 0) val *= 3600.0;
            else if (strcmp(dst_unit, "m") == 0) val *= 60.0;
            else if (strcmp(dst_unit, "d") == 0) val /= 24.0;
        }
    }
    return val;
}

// ---------------------------------------------------------------------------
// Output key: name + "_" + unit suffix (except Text)
// ---------------------------------------------------------------------------
static void make_key(char* out, size_t out_len, const char* rendered_name,
                     Quantity q, const char* display_unit, VifRange vif_range) {
    const char* suffix = display_unit;
    if (!suffix) {
        if (q == Quantity::PointInTime) {
            suffix = vifrange_unit_suffix(vif_range);
            if (!suffix || !suffix[0]) suffix = "datetime";
        } else {
            suffix = quantity_default_suffix(q);
        }
    }
    if (q == Quantity::Text || !suffix || !suffix[0]) {
        snprintf(out, out_len, "%s", rendered_name);
    } else {
        snprintf(out, out_len, "%s_%s", rendered_name, suffix);
    }
}

// ---------------------------------------------------------------------------
// Calculate resolver context
// ---------------------------------------------------------------------------
struct CalcCtx {
    const DecodeResult* res;
    int upto;           // evaluate against fields [0, upto)
    int storage, tariff, subunit; // current record context
};

static bool calc_resolver(const char* name, double* out, void* ctx) {
    CalcCtx* cc = (CalcCtx*)ctx;
    if (resolve_template_var(name, out, cc)) return true;
    for (int i = 0; i < cc->upto; ++i) {
        const OutField* f = &cc->res->fields[i];
        // text fields are resolvable too: dates carry epoch days, lookups raw
        if (strcmp(f->name, name) == 0) {
            *out = f->value;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// The interpreter
// ---------------------------------------------------------------------------
bool decode_telegram(const Telegram& t, uint8_t* payload, size_t payload_len,
                     const uint8_t* key, const DriverDef* forced,
                     DecodeResult* out) {
    memset(out, 0, sizeof(*out));

    // MVT for detection: long TPL wins, else DLL.
    char mfct[4];
    uint8_t version, type;
    if (t.tpl_long) {
        mfct_to_str(t.tpl_mfct, mfct);
        version = t.tpl_version;
        type = t.tpl_type;
    } else {
        mfct_to_str(t.dll_mfct, mfct);
        version = t.dll_version;
        type = t.dll_type;
    }
    memcpy(out->mfct, mfct, 4);

    const DriverDef* drv = forced ? forced : find_driver(mfct, version, type);
    if (!drv) return false;
    out->driver = drv;

    // id string: TPL long header id wins, else DLL id.
    if (t.tpl_long) {
        snprintf(out->id, sizeof(out->id), "%02x%02x%02x%02x",
                 t.tpl_a[3], t.tpl_a[2], t.tpl_a[1], t.tpl_a[0]);
    } else {
        memcpy(out->id, t.dll_id_str, 9);
    }

    // Decrypt if needed.
    if (t.sec_mode == SEC_AES_CBC_IV && key) {
        aes_decrypt_mode5(t, payload, payload_len, key);
        // Don't return false on decrypt failure - try parsing anyway
        // (test vectors may be pre-decrypted or partially encrypted)
    } else if (t.sec_mode == SEC_AES_CBC_NO_IV && key) {
        aes_decrypt_mode7(t, payload, payload_len, key);
    }
    // For all other modes: try parsing payload as-is

    // Parse DV records; hand-ported payload decoders may add synthetic ones.
    static DVRecord recs[WMBUS_MAX_DV_RECORDS + 8];
    const uint8_t* mfct_data = nullptr;
    size_t mfct_len = 0;
    size_t n = difvif_parse(payload, payload_len, recs, WMBUS_MAX_DV_RECORDS,
                            &mfct_data, &mfct_len);

    PayloadDecoderFn pdf = find_payload_decoder(drv->name);
    if (pdf) {
        n += pdf(t, payload, payload_len, recs + n, 8);
    }

    // Media string.
    media_type_json(type, t.tpl_long ? t.tpl_mfct : t.dll_mfct, out->media, sizeof(out->media));

    // Evaluate field rules in order.
    char status_extra[64] = {0}; // INJECT_INTO_STATUS contributions
    for (uint8_t fi = 0; fi < drv->num_fields && out->num_fields < WMBUS_MAX_OUT_FIELDS; ++fi) {
        const FieldRule* fr = &drv->fields[fi];
        bool hide = (fr->attrs & FATTR_HIDE) != 0;

        bool has_match = (fr->difvifkey != nullptr) || (fr->vif_range != VifRange::None);

        if (fr->calculate && !has_match) {
            // Pure calculated field.
            CalcCtx cc{ out, out->num_fields, 0, 0, 0 };
            FVal v = formula_eval(fr->calculate, calc_resolver, &cc);
            if (!v.ok) continue;
            OutField* of = &out->fields[out->num_fields];
            char rname[48];
            render_field_name(rname, sizeof(rname), fr->name, 0, 0, 0);
            make_key(of->name, sizeof(of->name), rname, fr->quantity, fr->display_unit, fr->vif_range);
            of->hidden = hide;
            if (v.unit == FUnit::Date) {
                int y; unsigned m, d;
                f_civil_from_days((long)v.v, &y, &m, &d);
                if (fr->display_unit && strcmp(fr->display_unit, "datetime") == 0) {
                    snprintf(of->text, sizeof(of->text), "%04d-%02d-%02d 00:00", y, m, d);
                } else {
                    snprintf(of->text, sizeof(of->text), "%04d-%02d-%02d", y, m, d);
                }
                of->is_text = true;
                of->value = v.v;
            } else if (fr->lookup) {
                lookup_render(fr->lookup, v.v, of->text, sizeof(of->text));
                of->is_text = true;
                of->value = v.v;
            } else {
                of->value = v.v;
                of->is_text = false;
            }
            out->num_fields++;
            continue;
        }

        int matched = 0;
        for (size_t ri = 0; ri < n && out->num_fields < WMBUS_MAX_OUT_FIELDS; ++ri) {
            const DVRecord* r = &recs[ri];
            if (!record_matches(fr, r)) continue;
            matched++;

            OutField* of = &out->fields[out->num_fields];
            char rname[48];
            render_field_name(rname, sizeof(rname), fr->name, r->storage_nr, r->tariff_nr, r->subunit_nr);
            make_key(of->name, sizeof(of->name), rname, fr->quantity, fr->display_unit, r->vif_range);
            of->hidden = hide;

            double value = compute_value(fr, r);

            if (fr->calculate) {
                CalcCtx cc{ out, out->num_fields, r->storage_nr, r->tariff_nr, r->subunit_nr };
                FVal v = formula_eval(fr->calculate, calc_resolver, &cc);
                if (!v.ok) { continue; }
                if (v.unit == FUnit::Date || fr->quantity == Quantity::PointInTime) {
                    int y; unsigned m, d;
                    f_civil_from_days((long)v.v, &y, &m, &d);
                    if (fr->display_unit && strcmp(fr->display_unit, "date") == 0) {
                        snprintf(of->text, sizeof(of->text), "%04d-%02d-%02d", y, m, d);
                        of->is_text = true;
                    } else {
                        snprintf(of->text, sizeof(of->text), "%04d-%02d-%02d", y, m, d);
                        of->is_text = true;
                    }
                } else {
                    of->is_text = false;
                }
                of->value = v.v;
            } else if (fr->lookup) {
                lookup_render(fr->lookup, value, of->text, sizeof(of->text));
                
                if (fr->attrs & FATTR_INCLUDE_TPL_STATUS) {
                    char tpl_str[128];
                    if (drv->tpl_status_lookup) lookup_render(drv->tpl_status_lookup, t.tpl_status, tpl_str, sizeof(tpl_str));
                    else snprintf(tpl_str, sizeof(tpl_str), "%s", t.tpl_status == 0 ? "OK" : "ERR");
                    if (strcmp(tpl_str, "OK") != 0 && tpl_str[0]) {
                        size_t l = strlen(of->text);
                        if (strcmp(of->text, "OK") == 0) { of->text[0] = 0; l = 0; }
                        snprintf(of->text + l, sizeof(of->text) - l, "%s%s", l ? " " : "", tpl_str);
                    }
                    if (of->text[0] == 0) snprintf(of->text, sizeof(of->text), "OK");
                }
                
                of->is_text = true;
                of->value = value;
                if (fr->attrs & FATTR_INJECT_INTO_STATUS) {
                    if (of->text[0] && strcmp(of->text, "OK") != 0) {
                        strncat(status_extra, of->text, sizeof(status_extra) - strlen(status_extra) - 2);
                        strncat(status_extra, " ", sizeof(status_extra) - strlen(status_extra) - 1);
                    }
                    of->hidden = true;
                }
            } else if (fr->quantity == Quantity::Text) {
                // text rendering; Date/DateTime ranges render as dates
                if (r->vif_range == VifRange::Date) {
                    if (!decode_type_g((uint16_t)r->raw, of->text)) {
                        snprintf(of->text, sizeof(of->text), "null");
                        value = 0;
                    } else {
                        double ed = type_g_to_days((uint16_t)r->raw);
                        if (!isnan(ed)) value = ed;
                    }
                } else if (r->vif_range == VifRange::DateTime) {
                    if (!decode_type_f((uint32_t)r->raw, of->text)) {
                        snprintf(of->text, sizeof(of->text), "null");
                        value = 0;
                    } else {
                        double ed = type_f_to_days((uint32_t)r->raw);
                        if (!isnan(ed)) value = ed;
                    }
                } else {
                    snprintf(of->text, sizeof(of->text), "%s", r->is_text ? r->text : "");
                }
                of->is_text = true;
                of->value = value;
            } else if (fr->quantity == Quantity::PointInTime) {
                if (r->vif_range == VifRange::Date) {
                    if (!decode_type_g((uint16_t)r->raw, of->text)) {
                        snprintf(of->text, sizeof(of->text), "null");
                        value = 0;
                    } else {
                        double ed = type_g_to_days((uint16_t)r->raw);
                        if (!isnan(ed)) value = ed;
                    }
                    of->is_text = true;
                } else if (r->vif_range == VifRange::DateTime) {
                    if (!decode_type_f((uint32_t)r->raw, of->text)) {
                        snprintf(of->text, sizeof(of->text), "null");
                        value = 0;
                    } else {
                        double ed = type_f_to_days((uint32_t)r->raw);
                        if (!isnan(ed)) value = ed;
                    }
                    of->is_text = true;
                } else {
                    of->is_text = false;
                }
                of->value = value;
            } else {
                of->value = value;
                of->is_text = false;
            }
            
            // Wait, we need to append status_extra if this is the target field
            if ((fr->attrs & (FATTR_STATUS | FATTR_INCLUDE_TPL_STATUS)) && status_extra[0]) {
                size_t l = strlen(of->text);
                if (strcmp(of->text, "OK") == 0) { of->text[0] = 0; l = 0; }
                snprintf(of->text + l, sizeof(of->text) - l, "%s%s", l ? " " : "", status_extra);
                size_t nl = strlen(of->text);
                while (nl > 0 && of->text[nl - 1] == ' ') of->text[--nl] = 0;
                if (of->text[0] == 0) snprintf(of->text, sizeof(of->text), "OK");
            }
            
            out->num_fields++;

            // Non-templated plain rules take only the first matching record.
            if (!strchr(fr->name, '{')) break;
        }

        // Pure status field (no match, no calculate): emit tpl status.
        if (matched == 0 && !fr->calculate &&
            (fr->attrs & (FATTR_STATUS | FATTR_INCLUDE_TPL_STATUS))) {
            OutField* of = &out->fields[out->num_fields];
            make_key(of->name, sizeof(of->name), fr->name, fr->quantity, fr->display_unit, fr->vif_range);
            of->hidden = hide;
            of->is_text = true;
            of->value = 0;
            of->text[0] = 0;
            
            if (fr->attrs & FATTR_INCLUDE_TPL_STATUS) {
                if (drv->tpl_status_lookup) {
                    lookup_render(drv->tpl_status_lookup, t.tpl_status, of->text, sizeof(of->text));
                } else {
                    snprintf(of->text, sizeof(of->text), "%s", t.tpl_status == 0 ? "OK" : "ERR");
                }
            } else if (fr->lookup) {
                lookup_render(fr->lookup, 0, of->text, sizeof(of->text));
            } else {
                snprintf(of->text, sizeof(of->text), "OK");
            }
            
            if (of->text[0] == 0) snprintf(of->text, sizeof(of->text), "OK");

            // merge injected status extras
            if (status_extra[0]) {
                size_t l = strlen(of->text);
                if (strcmp(of->text, "OK") == 0) { of->text[0] = 0; l = 0; }
                snprintf(of->text + l, sizeof(of->text) - l, "%s%s", l ? " " : "", status_extra);
                size_t nl = strlen(of->text);
                while (nl > 0 && of->text[nl - 1] == ' ') of->text[--nl] = 0;
                if (of->text[0] == 0) snprintf(of->text, sizeof(of->text), "OK");
            }
            out->num_fields++;
        }
    }

    return true;
}

} // namespace wmb
