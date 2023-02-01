
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cstring>
#include "struct-bindgen/gen-c-struct-binding.h"
#include <sstream>

extern "C" {
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
}

using namespace eunomia;
#define BUFFER_SIZE 1024

void
c_struct_json_generator::start_generate(std::string &output)
{
    auto header = get_c_file_header();
    header += R"(
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "cJSON.h"
)";
    output += header;
}

void
c_struct_json_generator::end_generate(std::string &output)
{
    output += get_c_file_footer();
}

void
c_struct_json_generator::enter_struct_def(std::string &output, struct_info info)
{
    if (walk_count == 0) {
        // generate marshal function
        const char *function_proto = R"(
static char *
marshal_struct_%s__to_json_str(const struct %s *src)
{
    assert(src);
    cJSON *object = cJSON_CreateObject();
    if (!object) {
        return NULL;
    }
)";
        char struct_def[BUFFER_SIZE];
        snprintf(struct_def, sizeof(struct_def), function_proto,
                 info.struct_name, info.struct_name);
        output += struct_def;
    }
    else {
        // generate unmarshal function
        const char *function_proto = R"(
static struct %s *
unmarshal_struct_%s__from_json_str(struct %s *dst, const char *src)
{
    assert(dst && src);
    cJSON *object = cJSON_Parse(src);
    if (!object) {
        return NULL;
    }
)";
        char struct_def[BUFFER_SIZE];
        snprintf(struct_def, sizeof(struct_def), function_proto,
                 info.struct_name, info.struct_name, info.struct_name);
        output += struct_def;
    }
}

void
c_struct_json_generator::exit_struct_def(std::string &output, struct_info info)
{
    if (walk_count == 0) {
        // generate marshal function
        output = output + R"(
    return cJSON_PrintUnformatted(object);
)";
    }
    else {
        // generate unmarshal function
        output = output + R"(
    return dst;
)";
    }
    output += "}\n";
}

void
c_struct_json_generator::marshal_json_struct(std::string &output,
                                             field_info info,
                                             const char *base_json_name)
{
    const char *format = R"(
    char *%s_object_str = marshal_struct_%s__to_json_str(&src->%s);
    if (!%s_object_str) {
        cJSON_Delete(%s);
        return NULL;
    }
    cJSON *%s_object = cJSON_Parse(%s_object_str);
    if (!%s_object) {
        cJSON_Delete(%s);
        return NULL;
    }
    if (!cJSON_AddItemToObject(object, "%s", %s_object)) {
        cJSON_Delete(%s);
        return NULL;
    }
)";
    default_printer.printf(format, info.field_name, info.field_type_name,
                           info.field_name, info.field_name, base_json_name,
                           info.field_name, info.field_name, info.field_name,
                           base_json_name, info.field_name, info.field_name,
                           base_json_name);
}

void
c_struct_json_generator::marshal_json_type(std::string &output, field_info info,
                                           const char *json_type_str,
                                           const char *type_conversion,
                                           const char *base_json_name,
                                           bool is_array)
{
    const char *format = R"(
    cJSON *%s_object = cJSON_Create%s(%ssrc->%s);
    if (!%s_object) {
        cJSON_Delete(%s);
        return NULL;
    }
    if (!cJSON_AddItemTo%s(object, "%s", %s_object)) {
        cJSON_Delete(%s);
        return NULL;
    }
)";
    const char *add_to = is_array ? "Array" : "Object";
    default_printer.printf(format, info.field_name, json_type_str,
                           type_conversion, info.field_name, info.field_name,
                           base_json_name, add_to, info.field_name,
                           info.field_name, base_json_name);
}

void
c_struct_json_generator::unmarshal_json_type(std::string &output,
                                             field_info info,
                                             const char *json_type_str,
                                             const char *type_conversion,
                                             const char *base_json_name,
                                             const char *value_type_str)
{
    const char *format = R"(
    cJSON *%s_object = cJSON_GetObjectItemCaseSensitive(object, "%s");
    if (!cJSON_Is%s(%s_object)) {
        cJSON_Delete(object);
        return NULL;
    }
    dst->%s = %s%s_object->%s;
)";
    default_printer.printf(format, info.field_name, info.field_name,
                           json_type_str, info.field_name, info.field_name,
                           type_conversion, info.field_name, value_type_str);
}

void
c_struct_json_generator::marshal_field(std::string &output, field_info info)
{
    uint32_t offset = info.bit_off / 8;
    default_printer.reset();
    auto var = btf__type_by_id(btf_data, info.type_id);

    if (btf_is_int(var) || btf_is_float(var)) {
        marshal_json_type(output, info, "Number", "", "object", false);
    }
    else if (btf_is_ptr(var)) {
        marshal_json_type(output, info, "Number", "(long long int)", "object",
                          false);
    }
    else if (btf_is_array(var)) {
        // is char array
        // TODO
    }
    else if (btf_is_struct(var)) {
        marshal_json_struct(output, info, "object");
    }
    else if (btf_is_union(var)) {
        // TODO: add more type support
        throw std::runtime_error("union is not supported");
    }
    else {
        throw std::runtime_error("unknown type");
    }
    output += default_printer.buffer;
}

void
c_struct_json_generator::unmarshal_field(std::string &output, field_info info)
{
    uint32_t offset = info.bit_off / 8;
    default_printer.reset();
    auto var = btf__type_by_id(btf_data, info.type_id);
    if (btf_is_int(var)) {
        unmarshal_json_type(output, info, "Number", "", "object", "valueint");
    }
    else if (btf_is_float(var)) {
        unmarshal_json_type(output, info, "Number", "", "object",
                            "valuedouble");
    }
    else if (btf_is_ptr(var)) {
        unmarshal_json_type(output, info, "Number", "(void *)", "object",
                            "valueint");
    }
    else if (btf_is_array(var)) {
        marshal_json_array(output, info.field_name, info.type_id, "object",
                           false);
    }
    else if (btf_is_struct(var)) {
        // TODO
    }
    else if (btf_is_union(var)) {
        throw std::runtime_error("union is not supported");
    }
    else {
        throw std::runtime_error("unknown type");
    }
    output += default_printer.buffer;
}

void
c_struct_json_generator::enter_struct_field(std::string &output,
                                            field_info info)
{
    if (info.bit_sz != 0) {
        std::cerr << "bitfield not supported" << std::endl;
        throw std::runtime_error("bitfield not supported");
    }
    if (walk_count == 0) {
        marshal_field(output, info);
    }
    else {
        unmarshal_field(output, info);
    }
}

void c_struct_json_generator::marshal_json_array(std::string& output,
                                                 const char* field_name,
                                                 int type_id,
                                                 const char* base_json_name,
                                                 bool base_json_is_array,
                                                 int dim) {
    using std::cerr;
    using std::endl;
    const btf_type* type_info = btf__type_by_id(this->btf_data, type_id);
    assert(btf_is_array(type_info));
    const struct btf_array* array_info = btf_array(type_info);
    const struct btf_type* elem_type =
        btf__type_by_id(this->btf_data, array_info->type);
    // std::osstr
    sprintf_printer& curr_printer = default_printer;
    curr_printer.reset();
    curr_printer.printf("{\n");
    char object_name[512];
    snprintf(object_name, sizeof(object_name), "%s_object_dim_%d", field_name,
             dim);

    curr_printer.printf(
        "for(int var_dim_%d = 0; var_dim_%d < %d; var_dim_%d ++) {\n", dim, dim,
        array_info->nelems, dim);

    const char* format = R"(
    cJSON *%s = cJSON_CreateArray(src->%s);
    if (! %s) {
        // cJSON_Delete(%s);
        return NULL;
    }

)";
    curr_printer.printf(format, object_name, field_name, object_name);
    if (btf_is_array(elem_type)) {
        marshal_json_array(output, field_name, elem_type->type, object_name,
                           true, dim + 1);
    } else if (btf_is_int(elem_type) || btf_is_float(elem_type) ||
               btf_is_ptr(elem_type)) {
        // TODO: common types
    } else {
        // TODO: struct arrays
        throw std::runtime_error("Support for struct arrays is WIP");
    }
    curr_printer.printf("}\n");
    output += curr_printer.buffer;
}