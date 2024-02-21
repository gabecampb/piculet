/*
Copyright (c) 2021-2024 Gabriel Campbell

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// assembler.c
// Gabriel Campbell (github.com/gabecampb)
// Created 2021-01-10

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

uint8_t view_unused_labels;					/* print all labels that are defined but not referenced in the program - command option -v */
uint8_t output_shader_binaries;				/* write assembler shader regions to files, in filename format _shader{region #} - command option -s */

uint8_t* output = 0;
uint64_t output_size = 0;

uint8_t current_preg = 0;
uint8_t current_sreg = 0;
uint8_t current_oreg = 0;

void add_8(uint8_t value) { // adds a byte to output
	output = realloc(output, output_size+1);
	output[output_size] = value;
	output_size++;
}

void add_16(uint16_t value) { // adds 2 bytes to output
	output = realloc(output, output_size+2);
	*(uint16_t*)(output+output_size) = value;
	output_size += 2;
}

void add_32(uint32_t value) { // adds 4 bytes to output
	output = realloc(output, output_size+4);
	*(uint32_t*)(output+output_size) = value;
	output_size += 4;
}

void add_64(uint64_t value) { // adds 8 bytes to output
	output = realloc(output, output_size+8);
	*(uint64_t*)(output+output_size) = value;
	output_size += 8;
}

void add_primary_set(uint8_t reg)   { current_preg = reg; add_8(0xA0+reg); } // add a primary register setter
void add_secondary_set(uint8_t reg) { current_sreg = reg; add_8(0xB0+reg); } // add a secondary register setter
void add_output_set(uint8_t reg)    { current_oreg = reg; add_8(0x00+reg); } // add an output register setter

// case insensitive comparsion of two null-terminated strings; returns 1 if equal and 0 otherwise
uint8_t compstr(char* a, char* b) {
	uint8_t equal = 1;
	for(uint32_t i = 0;; i++) {
		if(toupper(a[i]) != toupper(b[i])) { equal = 0; break; }
		if(a[i] == '\0' || b[i] == '\0') {
			equal = a[i] == b[i];
			break;
		}
	}
	return equal;
}

// case insensitive: does string a include b; returns 1 if so and 0 otherwise
uint8_t strincludes(char* a, char* b) {
	char* x = malloc(strlen(a)+1);
	char* y = malloc(strlen(b)+1);
	memcpy(x, a, strlen(a)+1);
	memcpy(y, b, strlen(b)+1);
	for(uint32_t i = 0; x[i] != '\0'; i++) x[i] = toupper(x[i]);
	for(uint32_t i = 0; y[i] != '\0'; i++) y[i] = toupper(y[i]);
	char* res = strstr(x,y);
	free(x); free(y);
	return res != 0;
}

char**		label_names = 0;
uint64_t*	label_addresses = 0;	// the address a label refers to in the output (wherever the label definition was)
uint32_t*	label_n_refs = 0;		// the number of times each label is referenced
uint64_t	n_labels = 0;			// total count of labels

uint64_t*	label_ref_addr = 0;		// for each label reference, address of the reference in the output
uint32_t*	label_ref_ids = 0;		// for each label reference, the ID of the label being referenced (index into label_addresses[])
uint8_t*	label_ref_regs = 0;		// for each label reference, the register to move the label value into (0=primary or 1=secondary)
uint64_t	n_label_refs = 0;		// total count of label references

void add_label(char* label_name) { // add a new label
	// check if label already exists
	uint8_t label_exists = 0;
	for(uint32_t i = 0; i < n_labels; i++)
		if(strcmp(label_name,label_names[i]) == 0) return; // nothing to do
	if(!label_exists) { // add the label
		label_names = realloc(label_names, sizeof(char*)*(n_labels+1));		// add a char* to the label_names char* array
		label_names[n_labels] = malloc(strlen(label_name)+1);				// allocate label string and assign to new char*
		memcpy(label_names[n_labels], label_name, strlen(label_name)+1);	// copy label name to the allocated string in label_names

		label_addresses = realloc(label_addresses, sizeof(uint64_t)*(n_labels+1));
		label_addresses[n_labels] = 0;		// set by set_label_address

		label_n_refs = realloc(label_n_refs, sizeof(uint32_t)*(n_labels+1));
		label_n_refs[n_labels] = 0;			// incremented by add_label_ref

		n_labels++;
	}
}

void set_label_address(char* label_name, uint64_t address) { // set a label's address
	for(uint32_t i = 0; i < n_labels; i++)
		if(strcmp(label_name,label_names[i]) == 0)
			label_addresses[i] = address;
}

void add_label_ref(uint8_t ref_reg, char* label_name, uint64_t ref_address) { // add a label reference
	for(uint32_t i = 0; i < n_labels; i++)
		if(strcmp(label_name,label_names[i]) == 0) {
			label_ref_addr = realloc(label_ref_addr, sizeof(uint64_t)*(n_label_refs+1));
			label_ref_ids = realloc(label_ref_ids, sizeof(uint32_t)*(n_label_refs+1));
			label_ref_regs = realloc(label_ref_regs, sizeof(uint8_t)*(n_label_refs+1));

			label_ref_addr[n_label_refs] = ref_address;
			label_ref_ids[n_label_refs] = i;
			label_ref_regs[n_label_refs] = ref_reg;

			label_n_refs[i]++;
			n_label_refs++;
			return;
		}
}

// get an existing label's address
// returns 1 if the label exists, 0 otherwise
uint8_t check_label(char* label_name) {
	for(uint32_t i = 0; i < n_labels; i++)
		if(strcmp(label_name, label_names[i]) == 0)
			return 1;
	return 0;
}

// get minimum # of bytes (1,2,4, or 8) required to represent a value
uint8_t get_value_size(uint64_t value) {
	if(value <= 0xFF)		return 1;
	if(value <= 0xFFFF)		return 2;
	if(value <= 0xFFFFFFFF)	return 4;
	return 8;
}

// returns 0 if str is a valid base 10 positive integer, or 1 otherwise
uint8_t check_str_uint(char* str) {
	if((str[0] == '0' && strlen(str) != 1) || str[0] == '\0') return 1;
	for(uint32_t i = 0; i < strlen(str); i++)
		if(!isdigit(str[i])) return 1;
	uint64_t val = strtoull(str,0,10);
	if(val > UINT32_MAX) return 1;
	return 0;
}

// returns 0 if str is a valid base 10 positive short, or 1 otherwise
uint8_t check_str_ushort(char* str) {
	if((str[0] == '0' && strlen(str) != 1) || str[0] == '\0') return 1;
	for(uint32_t i = 0; i < strlen(str); i++)
		if(!isdigit(str[i])) return 1;
	uint64_t val = strtoull(str,0,10);
	if(val > UINT16_MAX) return 1;
	return 0;
}

// returns 0 if str is a valid base 10 positive byte, or 1 otherwise
uint8_t check_str_ubyte(char* str) {
	if((str[0] == '0' && strlen(str) != 1) || str[0] == '\0') return 1;
	for(uint32_t i = 0; i < strlen(str); i++)
		if(!isdigit(str[i])) return 1;
	uint64_t val = strtoull(str,0,10);
	if(val > UINT8_MAX) return 1;
	return 0;
}

// returns 0 if str is a valid base 10 positive/negative integer, or 1 otherwise
uint8_t check_str_int(char* str) {
	if(str[0] == '\0' || str[0] == '0' || (str[0] == '-' && str[1] == '0')) return 1;
	for(uint32_t i = 0; i < strlen(str); i++)
		if(!isdigit(str[i]) && str[i] != '-') return 1;
	uint64_t val = strtoull(str,0,10);
	if(val < INT32_MIN || val > INT32_MAX) return 1;
	return 0;
}

// returns the constant specified in str, and sets its size, in bytes (1, 2, 4, or 8). size is returned as 0 on error.
// if size is not 0, it states the constant's size (this only applies to integer/hex values; if out of the range of integers of specified size it is an error and size is set to 0)
// flt, if present, is set to 1 if floating-point and 0 otherwise.
uint64_t get_constant(char* str, uint8_t* size, uint8_t* flt) {
	uint8_t constant_size = *size; // if 0, the size will be the minimum number of bytes required to hold the value
	*size = 0; // represents that an error had occurred
	if(flt) *flt = 0;
	uint32_t str_length = strlen(str); // this will be at least 1 character long
	// make sure everything is valid
	uint8_t contains_decimal = 0;
	for(uint32_t i = 0; i < str_length; i++)
		if(str[i] == '.') contains_decimal = 1;

	if(str[0] == '#') { // add hexadecimal constant
		// search for illegal characters
		if(str_length < 2) return 0;
		uint32_t first_dig = 0;
		for(uint32_t i = 1; i < str_length; i++) {
			if(str[i] == '-' && i != 1) return 0;
			if(str[i] != '-' && !isdigit(str[i]) && (toupper(str[i]) < 'A' || toupper(str[i]) > 'F')) return 1;
		}
		// there are no illegal characters
		uint64_t* ret = calloc(1,8);
		if(str[1] == '-') {
			int64_t value = strtoll(str+1,0,16);
			switch(constant_size) { // make sure, if there's a set limit on size of the constant, that the full value can fit
				case 0: break; // there is no limit on the size of the constant
				case 1: if(value < INT8_MIN  || value > INT8_MAX)  return 0; break;
				case 2: if(value < INT16_MIN || value > INT16_MAX) return 0; break;
				case 4: if(value < INT32_MIN || value > INT32_MAX) return 0; break;
				case 8: if(value < INT64_MIN || value > INT64_MAX) return 0; break;
			}
			memcpy(ret,&value,8);
		}
		else { 
			uint64_t value = strtoull(str+1,0,16);
			switch(constant_size) { // make sure, if there's a set limit on size of the constant, that the full value can fit
				case 0: break; // there is no limit on the size of the constant
				case 1: if(value < 0 || value > UINT8_MAX)  return 0; break;
				case 2: if(value < 0 || value > UINT16_MAX) return 0; break;
				case 4: if(value < 0 || value > UINT32_MAX) return 0; break;
				case 8: if(value < 0 || value > UINT64_MAX) return 0; break;
			}
			memcpy(ret,&value,8);
		}
		if(errno == ERANGE) return 0; // error; the value specified was too large to fit in an 8 byte integer
		uint64_t ret_val = *ret;
		free(ret);
		if(!constant_size) {
			*size = 8;
			if((ret_val & 0xFFFFFFFF00000000) == 0) *size = 4;
			if((ret_val & 0xFFFFFFFFFFFF0000) == 0) *size = 2;
			if((ret_val & 0xFFFFFFFFFFFFFF00) == 0) *size = 1;
		} else *size = constant_size;
		return ret_val;
	} else if(!contains_decimal) { // add integer constant
		// search for illegal characters
		if(str[0] == '0' && str_length != 1) return 1;
		for(uint32_t i = 0; i < str_length; i++) {
			if(str[i] == '-' && i != 0) return 1;
			if(str[i] != '-' && !isdigit(str[i]) && (toupper(str[i]) < 'A' || toupper(str[i]) > 'F')) return 1;
		}
		// there are no illegal characters
		uint64_t* ret = calloc(1,8);
		if(str[0] == '-') {
			int64_t value = strtoll(str,0,10);
			switch(constant_size) { // make sure, if there's a set limit on size of the constant, that the full value can fit
				case 0: break; // there is no limit on the size of the constant
				case 1: if(value < INT8_MIN  || value > INT8_MAX)  { *size = 0; return 0; } break;
				case 2: if(value < INT16_MIN || value > INT16_MAX) { *size = 0; return 0; } break;
				case 4: if(value < INT32_MIN || value > INT32_MAX) { *size = 0; return 0; } break;
				case 8: if(value < INT64_MIN || value > INT64_MAX) { *size = 0; return 0; } break;
			}
			memcpy(ret,&value,8);
		}
		else {
			uint64_t value = strtoull(str,0,10);
			switch(constant_size) { // make sure, if there's a set limit on size of the constant, that the full value can fit
				case 0: break; // there is no limit on the size of the constant
				case 1: if(value < 0 || value > UINT8_MAX)  { *size = 0; return 0; } break;
				case 2: if(value < 0 || value > UINT16_MAX) { *size = 0; return 0; } break;
				case 4: if(value < 0 || value > UINT32_MAX) { *size = 0; return 0; } break;
				case 8: if(value < 0 || value > UINT64_MAX) { *size = 0; return 0; } break;
			}
			memcpy(ret,&value,8);
		}
		if(errno == ERANGE) return 0; // error; the value specified was too large to fit in an 8 byte integer
		uint64_t ret_val = *ret;
		free(ret);
		if(!constant_size) {
			*size = 8;
			if((ret_val & 0xFFFFFFFF00000000) == 0) *size = 4;
			if((ret_val & 0xFFFFFFFFFFFF0000) == 0) *size = 2;
			if((ret_val & 0xFFFFFFFFFFFFFF00) == 0) *size = 1;
		} else *size = constant_size;
		return ret_val;
	} else { // add floating-point constant
		// search for illegal characters
		uint8_t found_decimal = 0;
		uint8_t found_digit = 0;
		for(uint32_t i = 0; i < str_length; i++) {
			if(str[i] == '-' && i != 0) return 0;
			if(toupper(str[i]) == 'D' && i != str_length-1) return 0;
			if(str[i] == '.' && found_decimal) return 0;
			if(str[i] == '.') found_decimal = 1;
			if(isdigit(str[i])) found_digit = 1;
			if(str[i] != '-' && str[i] != '.' && toupper(str[i]) != 'D' && !isdigit(str[i])) return 0;
		}
		if(!found_digit || !found_decimal) return 0;
		// there are no illegal characters
		uint64_t* ret = calloc(1,8);
		if(toupper(str[str_length-1]) == 'D') {
			double value = strtod(str,0); memcpy(ret,&value,8); *size = 8;
		} else { float value = strtof(str,0); memcpy(ret,&value,4); *size = 4; }
		if(errno == ERANGE) return 0; // error; the value specified was too large to fit in a floating-point
		if(flt) *flt = 1;
		uint64_t ret_val = *ret;
		free(ret);
		return ret_val;
	}
}

// check if a string is in form Rn (and that n is positive and a valid register number)
uint8_t check_str_reg(char* str) {
	if(strlen(str) < 2) return 1;
	if(toupper(str[0]) != 'R') return 1;
	if(check_str_uint(str+1) != 0) return 1;
	if(strtoull(str+1,0,10) > 15) return 1;
	return 0;
}

uint8_t shader_region = 0;
uint64_t* shader_region_starts = 0;
uint64_t* shader_region_ends = 0;
uint32_t n_shader_regions = 0;

// returns ID of a data type named in string 'type'. returns 255 if the string is not a valid type.
uint8_t type_id(char* type) {
	if(compstr(type,"VEC2")) return 0; if(compstr(type,"VEC3")) return 1;
	if(compstr(type,"VEC4")) return 2; if(compstr(type,"IVEC2")) return 3;
	if(compstr(type,"IVEC3")) return 4; if(compstr(type,"IVEC4")) return 5;
	if(compstr(type,"UVEC2")) return 6; if(compstr(type,"UVEC3")) return 7;
	if(compstr(type,"UVEC4")) return 8; if(compstr(type,"MAT2") || compstr(type,"MAT2X2")) return 9;
	if(compstr(type,"MAT2X3")) return 10; if(compstr(type,"MAT2X4")) return 11;
	if(compstr(type,"MAT3X2")) return 12; if(compstr(type,"MAT3") || compstr(type,"MAT3x3")) return 13;
	if(compstr(type,"MAT3X4")) return 14; if(compstr(type,"MAT4X2")) return 15;
	if(compstr(type,"MAT4X3")) return 16; if(compstr(type,"MAT4") || compstr(type,"MAT4x4")) return 17;
	if(compstr(type,"FLOAT")) return 18; if(compstr(type,"INT")) return 19;
	if(compstr(type,"UINT")) return 20; if(compstr(type,"SAMPLER")) return 21;
	if(compstr(type,"ISAMPLER")) return 22; if(compstr(type,"USAMPLER")) return 23;
	if(compstr(type,"IMAGE")) return 24; if(compstr(type,"ACCELSTRUCT")) return 25;
	return 0xFF;
}

// existing identifier strings (for shader regions; reset at the end of a shader region)
char** ids = 0;
uint16_t n_ids = 0;

// returns the index of an identifier string; adds if new
uint16_t add_identifier(char* identifier) {
	// check if identifier already exists
	for(uint16_t i = 0; i < n_ids; i++)
		if(compstr(identifier,ids[i]))
			return i;
	// add the identifier
	ids = realloc(ids, sizeof(char*)*(n_ids+1)); // add a char* to the ids char* array
	ids[n_ids] = malloc(strlen(identifier)+1); // allocate identifier string and assign to new char*
	memcpy(ids[n_ids], identifier, strlen(identifier)+1); // copy identifier to the allocated string in ids
	n_ids++;
	return n_ids - 1;
}

// given a shader index token, adds a shader index to the output.
// returns the number of bytes added to the output, and 0 on error
uint8_t add_index(char* index_str) {
	if(check_str_ushort(index_str) == 0) {
		add_16(strtoull(index_str,0,10));
		return 2;
	} else if(compstr(index_str,"ITR_IDX")) {
		// current loop iteration as index
		add_32(0xFFFFFFFF);
		return 4;
	} else if(strincludes(index_str,"INS_IDX")) {
		// index looks like: multiplier*INS_IDX+offset
		if(!isdigit(index_str[0])) return 0;	// error
		char* c = 0;
		uint16_t multiplier = strtoull(index_str,&c,10);
		if(*c != '*') return 0; // error
		c += strlen("INS_IDX")+1;
		if(*c != '-' && *c != '+') return 0; // error
		int32_t offset = strtoll(c,&c,10);
		if(*c != '\0') return 0; // error
		add_16(65533);
		if(!multiplier) return 0; // error
		add_16(multiplier-1);
		add_32(offset);
		return 8;
	} else {
		// use uint identifier as index, should have multiplier + offset (e.g. 5*var+1) around it if uint uniform or not if uint variable
		if(!isdigit(index_str[0])) {	// uint variable
			add_16(0xFFFF);
			add_16(add_identifier(index_str));
			return 4;
		}
		// uint uniform; multiplier*id+offset
		char* c = 0;
		uint16_t multiplier = strtoull(index_str,&c,10);
		if(*c != '*') return 0; // error
		char* a = ++c;
		while(*c != '+' && *c != '-' && *c != '\0') c++;
		if(*c == '\0') return 0; // error
		uint32_t len = (uint32_t)(c-a);
		char* id = malloc(len);
		memcpy(id, a, len);
		int32_t offset = strtoll(c,&c,10);
		if(*c != '\0') return 0;
		add_16(65534);
		add_16(add_identifier(id)); free(id);
		if(!multiplier) return 0; // error
		add_16(multiplier-1);
		add_32(offset);
		return 10;
	}
	return 0;
}

// returns 1 on error, 0 otherwise
uint8_t add_swizzle(char* swizzle) {
	uint8_t swizzle_byte = 0;
	uint32_t len = strlen(swizzle);
	if(len == 0 || len > 4) return 1;
	for(uint32_t i = 0; i < len; i++)
		switch(toupper(swizzle[i])) {
			case 'X': break;
			case 'Y': swizzle_byte |= 1 << (i*2); break; // y
			case 'Z': swizzle_byte |= 2 << (i*2); break; // z
			case 'W': swizzle_byte |= 3 << (i*2); break; // w
			default: return 1;
		};
	add_8(swizzle_byte);
	return 0;
}

// returns 1 on error, 0 otherwise
uint8_t process_shader_line(char* line, uint32_t line_num, char** tokens, uint32_t n_tokens) {
	// add the shader equivalent of each line to output; no validation other than if there is an equivalent shader statement
	// also do not know shader type, so allow everything
	// location/set/binding/element numbers are uint (check_str_ushort); constants are loaded using get_constant with a size limit of 4
	if(compstr(tokens[0],"LOC")) { // in/out attrib definition w/ location #
		if(n_tokens != 5) return 1;
		if(check_str_ushort(tokens[1]) != 0) return 1;
		if(compstr(tokens[2],"OUT")) // LOC # OUT type id (pixel shader output)
			add_8(0x01); // interpolation mode of pixel output does not matter
		else if(compstr(tokens[2],"IN")) // LOC # IN type id (vertex shader input)
			add_8(0x00);
		else return 1;
		if(type_id(tokens[3]) == 0xFF) return 1;
		add_8(type_id(tokens[3]));
		add_16(add_identifier(tokens[4]));
		add_16(strtoull(tokens[1],0,10));
	} else if(compstr(tokens[0],"IN") || compstr(tokens[0],"SMOOTH") || compstr(tokens[0],"FLAT") || compstr(tokens[0],"NOPERSP")) {
		// in/out attrib definition, no location #
		if(n_tokens != 3 && n_tokens != 4) return 1;
		if(compstr(tokens[0],"SMOOTH") || compstr(tokens[0],"FLAT") || compstr(tokens[0],"NOPERSP")) {
			if(n_tokens != 4) return 1; // INTERP_MODE OUT type id (vertex shader output)
			if(compstr(tokens[0],"FLAT")) add_8(0x01);
			else if(compstr(tokens[0],"SMOOTH")) add_8(0x02);
			else if(compstr(tokens[0],"NOPERSP")) add_8(0x03);
			else return 1;
			if(!compstr(tokens[1],"OUT")) return 1;
			if(type_id(tokens[2]) == 0xFF) return 1;
			add_8(type_id(tokens[2]));
			add_16(add_identifier(tokens[3]));
		} else if(compstr(tokens[0],"IN")) {
			if(n_tokens != 3) return 1; // IN type id (pixel shader input)
			if(type_id(tokens[1]) == 0xFF) return 1;
			add_8(0x00);
			add_8(type_id(tokens[1]));
			add_16(add_identifier(tokens[2]));
		} else return 1;
	} else if(compstr(tokens[0],"UNIFORM")) {
		// UNIFORM type id elcount
		if(n_tokens != 4) return 1;
		if(type_id(tokens[1]) == 0xFF) return 1;
		if(check_str_ushort(tokens[3]) != 0) return 1;
		add_8(0x04);
		add_8(type_id(tokens[1]));
		add_16(add_identifier(tokens[2]));
		add_16(strtoull(tokens[3],0,10));
	} else if(type_id(tokens[0]) != 0xFF) {
		// variable definition; type id elcount
		if(n_tokens != 3) return 1;
		if(check_str_ushort(tokens[2]) != 0) return 1;
		add_8(0x05);
		add_8(type_id(tokens[0]));
		add_16(add_identifier(tokens[1]));
		add_16(strtoull(tokens[2],0,10));
	} else if(compstr(tokens[0], "RAY_ATTR")) {
		// RAY_ATTR type id elcount
		if(n_tokens != 4) return 1;
		if(check_str_ushort(tokens[3]) != 0) return 1;
		if(type_id(tokens[1]) == 0xFF) return 1;
		add_8(0x06);
		add_8(type_id(tokens[1]));
		add_16(add_identifier(tokens[2]));
		add_16(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "UNIFORM_BLOCK")) {
		// UNIFORM_BLOCK set binding
		if(n_tokens != 3) return 1;
		if(check_str_ubyte(tokens[1]) != 0) return 1;
		if(check_str_uint(tokens[2]) != 0) return 1;
		add_8(0x07);
		add_8(strtoull(tokens[1],0,10));
		add_32(strtoull(tokens[2],0,10));
	} else if(compstr(tokens[0], "CLOSE")) {
		if(n_tokens != 1) return 1;
		add_8(0x08);
	} else if(compstr(tokens[0], "PUSH_BLOCK")) {
		if(n_tokens != 1) return 1;
		add_8(0x09);
	} else if(compstr(tokens[0], "STORAGE_BLOCK")) {
		// STORAGE_BLOCK set binding
		if(n_tokens != 3) return 1;
		if(check_str_ubyte(tokens[1]) != 0) return 1;
		if(check_str_uint(tokens[2]) != 0) return 1;
		add_8(0x0A);
		add_8(strtoull(tokens[1],0,10));
		add_32(strtoull(tokens[2],0,10));
	} else if(compstr(tokens[0], "RAY_BLOCK")) {
		// RAY_BLOCK location
		if(n_tokens != 2) return 1;
		if(check_str_ushort(tokens[1]) != 0) return 1;
		add_8(0x0B);
		add_16(strtoull(tokens[1],0,10));
	} else if(compstr(tokens[0], "INCOMING_RAY_BLOCK")) {
		// INCOMING_RAY_BLOCK location
		if(n_tokens != 2) return 1;
		if(check_str_ushort(tokens[1]) != 0) return 1;
		add_8(0x0C);
		add_16(strtoull(tokens[1],0,10));
	} else if(compstr(tokens[0], "FUNC")) {
		// FUNC id [params] OPEN
		if(n_tokens < 3) return 1;
		add_8(0x0D);
		add_16(add_identifier(tokens[1]));
		uint32_t t = 2;
		for(;; t += 4) { // cycle through and add parameter definitions
			// in/out/inout type id elcount
			if(t > n_tokens-1) return 1;
			if(compstr(tokens[t], "IN")) add_8(0x0F);
			else if(compstr(tokens[t], "OUT")) add_8(0x10);
			else if(compstr(tokens[t], "INOUT")) add_8(0x11);
			else break;
			if(type_id(tokens[t+1]) == 0xFF) return 1;
			if(check_str_ushort(tokens[t+3]) != 0) return 1;
			add_8(type_id(tokens[t+1]));
			add_16(add_identifier(tokens[t+2]));
			add_16(strtoull(tokens[t+3],0,10));
		}
		if(!compstr(tokens[t],"OPEN")) return 1;
		add_8(0x0E);
	} else if(compstr(tokens[0], "CALL")) {
		// CALL func [ids]
		if(n_tokens < 2) return 1;
		add_8(0x12);
		add_16(add_identifier(tokens[1]));
		for(uint32_t t = 2;; t++) {
			if(t > n_tokens-1) break;
			if(compstr(tokens[t], "END_CALL")) break;
			add_16(add_identifier(tokens[t]));
		}
	} else if(compstr(tokens[0], "RET")) {
		if(n_tokens != 1) return 1;
		add_8(0x13);
	} else if(compstr(tokens[0], "DISCARD")) {
		if(n_tokens != 1) return 1;
		add_8(0x73);
	} else if(compstr(tokens[0], "MAIN")) {
		if(n_tokens != 1) return 1;
		add_8(0x14);
	} else if(compstr(tokens[0], "VECOP_NEGATE")) {
		if(n_tokens != 3) return 1;
		add_8(0x15);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ABS")) {
		if(n_tokens != 3) return 1;
		add_8(0x16);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_NORM")) {
		if(n_tokens != 3) return 1;
		add_8(0x17);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_FLOOR")) {
		if(n_tokens != 3) return 1;
		add_8(0x18);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_CEIL")) {
		if(n_tokens != 3) return 1;
		add_8(0x19);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_TAN")) {
		if(n_tokens != 3) return 1;
		add_8(0x1A);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_SIN")) {
		if(n_tokens != 3) return 1;
		add_8(0x1B);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_COS")) {
		if(n_tokens != 3) return 1;
		add_8(0x1C);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ATAN")) {
		if(n_tokens != 3) return 1;
		add_8(0x1D);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ASIN")) {
		if(n_tokens != 3) return 1;
		add_8(0x1E);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ACOS")) {
		if(n_tokens != 3) return 1;
		add_8(0x1F);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_TANH")) {
		if(n_tokens != 3) return 1;
		add_8(0x20);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_SINH")) {
		if(n_tokens != 3) return 1;
		add_8(0x21);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_COSH")) {
		if(n_tokens != 3) return 1;
		add_8(0x22);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ATANH")) {
		if(n_tokens != 3) return 1;
		add_8(0x23);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ASINH")) {
		if(n_tokens != 3) return 1;
		add_8(0x24);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_ACOSH")) {
		if(n_tokens != 3) return 1;
		add_8(0x25);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_NLOG")) {
		if(n_tokens != 3) return 1;
		add_8(0x26);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "VECOP_LOG2")) {
		if(n_tokens != 3) return 1;
		add_8(0x27);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "MATOP_INV")) {
		if(n_tokens != 5) return 1;
		add_8(0x28);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
	} else if(compstr(tokens[0], "MATOP_DETERMINANT")) {
		if(n_tokens != 5) return 1;
		add_8(0x29);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
	} else if(compstr(tokens[0], "MATOP_TRANSPOSE")) {
		if(n_tokens != 5) return 1;
		add_8(0x2A);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
	} else if(compstr(tokens[0], "ADD")) {
		if(n_tokens != 10) return 1;
		add_8(0x2B);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1; 
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1; 
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "MULT")) {
		if(n_tokens != 10) return 1;
		add_8(0x2C);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "DIV")) {
		if(n_tokens != 10) return 1;
		add_8(0x2D);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "SUB")) {
		if(n_tokens != 10) return 1;
		add_8(0x2E);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "POW")) {
		if(n_tokens != 10) return 1;
		add_8(0x2F);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "ADD_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x30);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "MULT_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x31);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "DIV_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x32);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "SUB_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x33);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "POW_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x34);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "REV_ADD_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x35);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "REV_MULT_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x36);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "REV_DIV_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x37);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "REV_SUB_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x38);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "REV_POW_CONST")) {
		if(n_tokens != 8) return 1;
		add_8(0x39);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));		
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "SCALAROP_NEGATE")) {
		if(n_tokens != 4) return 1;
		add_8(0x3A);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ABS")) {
		if(n_tokens != 4) return 1;
		add_8(0x3B);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_RECIP")) {
		if(n_tokens != 4) return 1;
		add_8(0x3C);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_FLOOR")) {
		if(n_tokens != 4) return 1;
		add_8(0x3D);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_CEIL")) {
		if(n_tokens != 4) return 1;
		add_8(0x3E);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_TAN")) {
		if(n_tokens != 4) return 1;
		add_8(0x3F);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_SIN")) {
		if(n_tokens != 4) return 1;
		add_8(0x40);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_COS")) {
		if(n_tokens != 4) return 1;
		add_8(0x41);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ATAN")) {
		if(n_tokens != 4) return 1;
		add_8(0x42);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ASIN")) {
		if(n_tokens != 4) return 1;
		add_8(0x43);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ACOS")) {
		if(n_tokens != 4) return 1;
		add_8(0x44);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_TANH")) {
		if(n_tokens != 4) return 1;
		add_8(0x45);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_SINH")) {
		if(n_tokens != 4) return 1;
		add_8(0x46);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_COSH")) {
		if(n_tokens != 4) return 1;
		add_8(0x47);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ATANH")) {
		if(n_tokens != 4) return 1;
		add_8(0x48);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ASINH")) {
		if(n_tokens != 4) return 1;
		add_8(0x49);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_ACOSH")) {
		if(n_tokens != 4) return 1;
		add_8(0x4A);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_NLOG")) {
		if(n_tokens != 4) return 1;
		add_8(0x4B);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "SCALAROP_LOG2")) {
		if(n_tokens != 4) return 1;
		add_8(0x4C);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
	} else if(compstr(tokens[0], "CROSS")) {
		if(n_tokens != 7) return 1;
		add_8(0x4D);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
	} else if(compstr(tokens[0], "DOT")) {
		if(n_tokens != 7) return 1;
		add_8(0x4E);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
	} else if(compstr(tokens[0], "MATVEC_MULT")) {
		if(n_tokens != 7) return 1;
		add_8(0x4F);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
	} else if(compstr(tokens[0], "MAT_MULT")) {
		if(n_tokens != 7) return 1;
		add_8(0x50);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
	} else if(compstr(tokens[0], "SWIZZLE")) {
		if(n_tokens != 4) return 1;
		add_8(0x51);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(add_swizzle(tokens[3]) != 0) return 1;
	} else if(compstr(tokens[0], "ASSIGN_CONST")) {
		if(n_tokens != 5) return 1;
		add_8(0x52);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		uint8_t n_bytes = 4;
		uint8_t is_fp = 0;
		uint32_t val = get_constant(tokens[4], &n_bytes, &is_fp);
		if(is_fp && n_bytes != 4) return 1; // a double was specified here
		if(!n_bytes) return 1; // error with constant
		add_32(val);
	} else if(compstr(tokens[0], "ASSIGN_CONST_ARRAY")) {
		if(n_tokens < 5) return 1;
		add_8(0x53);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		if(check_str_ushort(tokens[2]) == 0) return 1;
		if(check_str_ubyte(tokens[3]) == 0) return 1;
		add_16(strtoull(tokens[2],0,10));
		add_8(strtoull(tokens[3],0,10));
		uint16_t n_constants = strtoull(tokens[3],0,10)+1;
		uint32_t t = 4;
		for(uint32_t t = 4; t < n_constants+4; t++) {
			if(t > n_tokens-1) return 1;
			uint8_t n_bytes = 4;
			uint8_t is_fp = 0;
			uint32_t val = get_constant(tokens[7], &n_bytes, &is_fp);
			if(is_fp && n_bytes != 4) return 1; // a double was specified here
			if(!n_bytes) return 1; // error with constant
			add_32(val);
		}
	} else if(compstr(tokens[0], "ASSIGN")) {
		// assign id idx vec/mat id idx vec/mat
		if(n_tokens != 7) return 1;
		add_8(0x54);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		if(!compstr(tokens[3], "NO_IDX") && check_str_ubyte(tokens[3]) != 0) return 1;
		else if(!compstr(tokens[3], "NO_IDX")) add_8(strtoull(tokens[3],0,10));
		add_16(add_identifier(tokens[4]));
		if(!compstr(tokens[5], "NO_IDX") && !add_index(tokens[5])) return 1;
		if(!compstr(tokens[6], "NO_IDX") && check_str_ubyte(tokens[6]) != 0) return 1;
		else if(!compstr(tokens[6], "NO_IDX")) add_8(strtoull(tokens[6],0,10));
	} else if(compstr(tokens[0], "VTX_OUT")) {
		if(n_tokens != 3) return 1;
		add_8(0x55);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "DEPTH_OUT")) {
		if(n_tokens != 3) return 1;
		add_8(0x56);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "GET_PRIM_ID")) {
		if(n_tokens != 3) return 1;
		add_8(0x57);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
	} else if(compstr(tokens[0], "IMAGE_READ")) {
		if(n_tokens != 6) return 1;
		add_8(0x58);
		add_16(add_identifier(tokens[1]));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
		add_16(add_identifier(tokens[4]));
		if(!add_index(tokens[5])) return 1;
	} else if(compstr(tokens[0], "IMAGE_WRITE")) {
		if(n_tokens != 6) return 1;
		add_8(0x59);
		add_16(add_identifier(tokens[1]));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
		add_16(add_identifier(tokens[4]));
		if(!add_index(tokens[5])) return 1;
	} else if(compstr(tokens[0], "GET_IMAGE_DIMS")) {
		if(n_tokens != 4) return 1;
		add_8(0x5A);
		add_16(add_identifier(tokens[1]));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
	} else if(compstr(tokens[0], "SAMPLE_LOD")) {
		if(n_tokens != 10) return 1;
		add_8(0x5B);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "SAMPLE")) {
		if(n_tokens != 10) return 1;
		add_8(0x5C);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "TEXEL_SAMPLE")) {
		if(n_tokens != 10) return 1;
		add_8(0x5D);
		add_16(add_identifier(tokens[1]));
		if(!compstr(tokens[2], "NO_IDX") && !add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
		add_16(add_identifier(tokens[7]));
		if(!compstr(tokens[8], "NO_IDX") && !add_index(tokens[8])) return 1;
		if(!compstr(tokens[9], "NO_IDX") && check_str_ubyte(tokens[9]) != 0) return 1;
		else if(!compstr(tokens[9], "NO_IDX")) add_8(strtoull(tokens[9],0,10));
	} else if(compstr(tokens[0], "GET_TEX_DIMS")) {
		if(n_tokens != 7) return 1;
		add_8(0x5E);
		add_16(add_identifier(tokens[1]));
		if(!add_index(tokens[2])) return 1;
		add_16(add_identifier(tokens[3]));
		if(!compstr(tokens[4], "NO_IDX") && !add_index(tokens[4])) return 1;
		add_16(add_identifier(tokens[5]));
		if(!compstr(tokens[6], "NO_IDX") && !add_index(tokens[6])) return 1;
	} else if(compstr(tokens[0], "IF") || compstr(tokens[0], "ELSE") || compstr(tokens[0], "ELSEIF")) {		// IF/ELSEIF/ELSE [conditions] OPEN
		if(n_tokens < 3) return 1;
		if(compstr(tokens[0], "IF")) add_8(0x5F);
		if(compstr(tokens[0], "ELSEIF")) add_8(0x60);
		if(compstr(tokens[0], "ELSE")) add_8(0x61);
		uint32_t t = 1;
		while(1) { // cycle through conditions list
			if(t > n_tokens-1) return 1;
			if(compstr(tokens[t], "OPEN")) break;
			// if tokens[t] is not a conjunction and t != 1, break
			if(compstr(tokens[t], "OR") || compstr(tokens[t], "AND")) { // if tokens[t] is a conjunction, add to output and t++
				if(compstr(tokens[t], "OR")) add_8(0x68);
				else if(compstr(tokens[t], "AND")) add_8(0x69);
				t++; if(t > n_tokens-1) return 1;
			} else {
				if(compstr(tokens[t],"CONST")) {
					if(t+1 > n_tokens-1) return 1;
					add_8(0);
					uint8_t n_bytes = 4;
					uint8_t is_fp = 0;
					uint32_t val = get_constant(tokens[t+1], &n_bytes, &is_fp);
					if(is_fp && n_bytes != 4) return 1; // a double was specified here
					if(!n_bytes) return 1; // error with constant
					add_32(val);
					t += 2;
				} else { // id index vec/mat_element
					if(t+2 > n_tokens-1) return 1;
					add_8(0x08);
					add_16(add_identifier(tokens[t]));
					if(!compstr(tokens[t+1], "NO_IDX") && !add_index(tokens[t+1])) return 1;
					if(!compstr(tokens[t+2], "NO_IDX") && check_str_ubyte(tokens[t+2]) != 0) return 1;
					else if(!compstr(tokens[t+2], "NO_IDX")) add_8(strtoull(tokens[t+2],0,10));
					t += 3;
				}
				if(t > n_tokens-1) return 1;
				// check relation operator, add to t
				if(compstr(tokens[t], ">")) add_8(0x62);
				else if(compstr(tokens[t], "<")) add_8(0x63);
				else if(compstr(tokens[t], "<=")) add_8(0x64);
				else if(compstr(tokens[t], ">=")) add_8(0x65);
				else if(compstr(tokens[t], "==")) add_8(0x66);
				else if(compstr(tokens[t], "!=")) add_8(0x67);
				else return 1;
				t++; if(t > n_tokens-1) return 1;
				// check value #2, add to t
				if(compstr(tokens[t],"CONST")) {
					if(t+1 > n_tokens-1) return 1;
					add_8(0);
					uint8_t n_bytes = 4;
					uint8_t is_fp = 0;
					uint32_t val = get_constant(tokens[t+1], &n_bytes, &is_fp);
					if(is_fp && n_bytes != 4) return 1; // a double was specified here
					if(!n_bytes) return 1; // error with constant
					add_32(val);
					t += 2;
				} else { // id index vec/mat_element
					if(t+2 > n_tokens-1) return 1;
					add_8(0x08);
					add_16(add_identifier(tokens[t]));
					if(!compstr(tokens[t+1], "NO_IDX") && !add_index(tokens[t+1])) return 1;
					if(!compstr(tokens[t+2], "NO_IDX") && check_str_ubyte(tokens[t+2]) != 0) return 1;
					else if(!compstr(tokens[t+2], "NO_IDX")) add_8(strtoull(tokens[t+2],0,10));
					t += 3;
				}

			}
		}
		if(!compstr(tokens[t],"OPEN")) return 1;
		add_8(0x0E);
	} else if(compstr(tokens[0], "LOOP")) {
		if(n_tokens != 3) return 1;
		if(check_str_ushort(tokens[1]) != 0) return 1;
		if(!compstr(tokens[2],"OPEN")) return 1;
		add_8(0x6A);
		add_16(strtoull(tokens[1],0,10));
		add_8(0x0E);
	} else if(compstr(tokens[0], "BREAK")) {
		if(n_tokens != 1) return 1;
		add_8(0x6B);
	} else if(compstr(tokens[0], "CONTINUE")) {
		if(n_tokens != 1) return 1;
		add_8(0x6C);
	} else if(compstr(tokens[0], "TRACE_RAY")) {
		if(n_tokens != 21) return 1;
		add_8(0x6D);
		add_16(add_identifier(tokens[1]));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
		add_16(add_identifier(tokens[4]));
		if(!add_index(tokens[5])) return 1;
		add_16(add_identifier(tokens[6]));
		if(!add_index(tokens[7])) return 1;
		add_16(add_identifier(tokens[8]));
		if(!add_index(tokens[9])) return 1;
		add_16(add_identifier(tokens[10]));
		if(!add_index(tokens[11])) return 1;
		add_16(add_identifier(tokens[12]));
		if(!add_index(tokens[13])) return 1;
		add_16(add_identifier(tokens[14]));
		if(!add_index(tokens[15])) return 1;
		add_16(add_identifier(tokens[16]));
		if(!add_index(tokens[17])) return 1;
		add_16(add_identifier(tokens[18]));
		if(!add_index(tokens[19])) return 1;
		if(check_str_ushort(tokens[20]) != 0) return 1;
		add_16(strtoull(tokens[20],0,10));
	} else if(compstr(tokens[0], "IGNORE_RAY")) {
		if(n_tokens != 1) return 1;
		add_8(0x6E);
	} else if(compstr(tokens[0], "TERM_RAY")) {
		if(n_tokens != 1) return 1;
		add_8(0x6F);
	} else if(compstr(tokens[0], "GET_RAY_INFO")) {
		if(n_tokens != 4) return 1;
		if(check_str_ubyte(tokens[1]) != 0) return 1;
		add_8(0x70);
		add_8(strtoull(tokens[1],0,10));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
	} else if(compstr(tokens[0],"BARRIER")) {
		if(n_tokens != 1) return 1;
		add_8(0x71);
	} else if(compstr(tokens[0], "GET_COMPUTE_INFO")) {
		if(n_tokens != 4) return 1;
		if(check_str_ubyte(tokens[1]) != 0) return 1;
		add_8(0x72);
		add_8(strtoull(tokens[1],0,10));
		add_16(add_identifier(tokens[2]));
		if(!add_index(tokens[3])) return 1;
	} else return 1;

	return 0;
}

uint8_t previous_label_def = 0; // first instruction after a label definition needs register setters prior

// returns 1 on error, 0 otherwise
uint8_t process_line(char* line, uint32_t line_num) { // reads a line and adds to output
	// SPLIT LINE INTO ARRAY OF STRINGS (TOKENS; EACH SEPARATED BY WHITESPACE); IGNORE ANYTHING PAST ; CHARACTER
	char** tokens = 0;
	uint32_t n_tokens = 0;
	int64_t token_start = -1; // where the current token started; -1 if position in line not on token

	for(uint32_t i = 0;; i++) {
		uint8_t token_char = (line[i] != ' ') && (line[i] != '\t') && (line[i] != '\n') && (line[i] != '\0') && (line[i] != ';');
		if(token_start == -1 && token_char) token_start = i; // start token
		if(token_start != -1 && !token_char) { // end token
			tokens = realloc(tokens, sizeof(char*)*(n_tokens+1));
			tokens[n_tokens] = malloc((i-token_start)+1);	// what is the number of bytes to allocate? (take into account the null character)
			memcpy(tokens[n_tokens], &line[token_start], (i-token_start)+1);
			tokens[n_tokens][i-token_start] = '\0';
			token_start = -1;
			n_tokens++;
		}
		if(line[i] == '\n' || line[i] == '\0' || line[i] == ';')
			break;
	}
	if(n_tokens == 0) return 0; // nothing on this line to add; return without error

	// check if line is a label definition
	uint8_t n_colons = 0;
	for(uint32_t i = 0;; i++) {
		if(line[i] == ':') n_colons++;
		if(line[i] == ':' && i == 0) return 1; // missing label name
		if(line[i] == '\n' || line[i] == '\0' || line[i] == ';') break;
	}
	if(n_colons > 1) return 1;
	if(n_colons && n_tokens != 1) return 1;
	else if(n_colons) {
		tokens[0][strlen(tokens[0])-1] = '\0';
		set_label_address(tokens[0],output_size);
		previous_label_def = 1;
		return 0; // set the label's address; return without error
	}

	// check if shaderstart or shaderend
	if(n_tokens == 1 && compstr(tokens[0], "SHADERSTART")) {
		if(shader_region) return 1; // shaderstart encountered while already in a shader region
		shader_region = 1;
		shader_region_starts = realloc(shader_region_starts, sizeof(uint64_t)*(n_shader_regions+1));
		shader_region_starts[n_shader_regions] = output_size;
		return 0;
	} else if(n_tokens == 1 && compstr(tokens[0], "SHADEREND")) {
		if(!shader_region) return 1; // shaderend encountered while not in a shader region
		shader_region = 0;
		shader_region_ends = realloc(shader_region_ends, sizeof(uint64_t)*(n_shader_regions+1));
		shader_region_ends[n_shader_regions] = output_size-1;
		n_shader_regions++;
		return 0;
	}
	if(shader_region) {
		if(process_shader_line(line,line_num,tokens,n_tokens) != 0) return 1;
		return 0;
	}

	// general form of an instruction: TOKEN p s o
	// each p,s,o register is represented by Rn,Rn,Rn
	// string literals and labels are treated differently

	// a line which is just a label_name: token will create a label; note label_name : is 2 tokens and therefore is not a valid label definition
	// "label_name rn" will move label address into a register
	// shaderstart will set shader_region, and shaderend will unset shader_region; if shaderstart is encountered while shader_region is set or shaderend while shader_region isn't set, the assembly is invalid
	// labels can be present within shader regions

	// check if line is a string literal
	char* last_token = tokens[n_tokens-1];
	if(tokens[0][0] == '\'' && last_token[strlen(last_token)-1] == '\'') { // this whole line is a string literal
		// add the line's contents from between first and last ' into binary and return 0
		// supported escape sequences: \n, \0, \\, \t
		int64_t start = -1; // index of first '
		int64_t end = -1; // index of last '
		for(uint32_t i = 0;; i++) {
			if(start == -1 && line[i] == '\'') start = i;
			if(line[i] == '\'') end = i;
			if(line[i] == '\n' || line[i] == '\0' || line[i] == ';') break;
		}
		for(uint32_t i = start+1; i < end; i++) add_8(line[i]);
		return 0;
	}

	// check if line is a constant definition
	if(n_tokens == 1) { // check if a floating-point constant; all other cases where there could be 1 token were already checked + returned from func prior to this
		uint8_t n_bytes = 0;
		uint8_t is_fp = 0;
		uint64_t constant = get_constant(tokens[0],&n_bytes,&is_fp);
		if(n_bytes) {
			if(!is_fp) {
				printf("error with constant on line %d.\n", line_num);
				return 1; // non-fp constant definitions must have 2 tokens
			}
			if(n_bytes == 4) add_32((uint32_t)constant); // add 32-bit floating-point constant to output
			if(n_bytes == 8) add_64((uint64_t)constant); // add 64-bit floating-point constant to output
			return 0; // added constant; return without error
		}
	} else if(n_tokens == 2) { // check if integer/hex constant
		uint8_t n_bytes = 0; // how many bytes the constant will occupy
		if(compstr(tokens[0], "B") || compstr(tokens[0], "H") || compstr(tokens[0], "W") || compstr(tokens[0], "D")) {
			switch(toupper(tokens[0][0])) {
				case 'B': n_bytes = 1; break;
				case 'H': n_bytes = 2; break;
				case 'W': n_bytes = 4; break;
				case 'D': n_bytes = 8; break;
			}
		}
		if(n_bytes) {
			uint8_t is_fp = 0;
			uint64_t constant = get_constant(tokens[1],&n_bytes,&is_fp);
			if(n_bytes == 0) {
				printf("error with constant on line %d.\n", line_num);
				return 1;
			}
			if(is_fp) return 1; // tokens[1] is a valid floating-point constant, but that is not allowed here
			if(n_bytes == 1) add_8((uint8_t)constant); // add 8-bit integer constant to output
			if(n_bytes == 2) add_16((uint16_t)constant); // add 16-bit integer constant to output
			if(n_bytes == 4) add_32((uint32_t)constant); // add 32-bit integer constant to output
			if(n_bytes == 8) add_64((uint64_t)constant); // add 64-bit integer constant to output
			return 0; // added constant; return without error	
		}
	}

// read a line of the common form: ins p=Rn s=Rn; define preg and sreg (set to the register numbers) and add the required register setters
#define READ_LINE_2_REGS if(n_tokens != 3) return 1; \
		if(check_str_reg(tokens[1]) != 0) return 1; \
		if(check_str_reg(tokens[2]) != 0) return 1; \
		uint8_t preg = strtoull(tokens[1]+1,0,10); \
		uint8_t sreg = strtoull(tokens[2]+1,0,10); \
		if(preg != current_preg) add_primary_set(preg); \
		if(sreg != current_sreg) add_secondary_set(sreg);

// read a line of the common form: ins p=Rn s=Rn o=Rn; define preg, sreg, and oreg (set to the register numbers) and add the required register setters
#define READ_LINE_3_REGS if(n_tokens != 4) return 1; \
		if(check_str_reg(tokens[1]) != 0) return 1; \
		if(check_str_reg(tokens[2]) != 0) return 1; \
		if(check_str_reg(tokens[3]) != 0) return 1; \
		uint8_t preg = strtoull(tokens[1]+1,0,10); \
		uint8_t sreg = strtoull(tokens[2]+1,0,10); \
		uint8_t oreg = strtoull(tokens[3]+1,0,10); \
		if(preg != current_preg) add_primary_set(preg); \
		if(sreg != current_sreg) add_secondary_set(sreg); \
		if(oreg != current_oreg) add_output_set(oreg);

{
	if(previous_label_def) { // in case there is a jump to a label, the p/s/o regs that were set when a label was previously defined need to be restored before an instruction
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
		previous_label_def = 0;
	}
	
	if(compstr(tokens[0], "LDB")) {	// LD p=Rn s=Rn (loads to p value at address s)
		READ_LINE_2_REGS;
		add_8(0xE0+0);
	} else if(compstr(tokens[0], "LDH")) {
		READ_LINE_2_REGS;
		add_8(0xE0+1);
	} else if(compstr(tokens[0], "LD")) {
		READ_LINE_2_REGS;
		add_8(0xE0+2);
	} else if(compstr(tokens[0], "LDD")) {
		READ_LINE_2_REGS;
		add_8(0xE0+3);
	} else if(compstr(tokens[0], "POPB")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0xE8+0);
	} else if(compstr(tokens[0], "POPH")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0xE8+1);
	} else if(compstr(tokens[0], "POP")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0xE8+2);
	} else if(compstr(tokens[0], "POPD")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0xE8+3);
	} else if(compstr(tokens[0], "STRB")) { // STR p=Rn s=Rn (stores value of p at address s)
		READ_LINE_2_REGS;
		add_8(0xF4+0);
	} else if(compstr(tokens[0], "STRH")) { // STR p=Rn s=Rn (stores value of p at address s)
		READ_LINE_2_REGS;
		add_8(0xF4+1);
	} else if(compstr(tokens[0], "STR")) { // STR p=Rn s=Rn (stores value of p at address s)
		READ_LINE_2_REGS;
		add_8(0xF4+2);
	} else if(compstr(tokens[0], "STRD")) { // STR p=Rn s=Rn (stores value of p at address s)
		READ_LINE_2_REGS;
		add_8(0xF4+3);
	} else if(compstr(tokens[0], "PUSHB")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg); 
		add_8(0xF8+0);
	} else if(compstr(tokens[0], "PUSHH")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg); 
		add_8(0xF8+1);
	} else if(compstr(tokens[0], "PUSH")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg); 
		add_8(0xF8+2);
	} else if(compstr(tokens[0], "PUSHD")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg); 
		add_8(0xF8+3);
	} else if(compstr(tokens[0], "MOV")) { // MOV Rn immediate_value
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		uint8_t is_label = check_label(tokens[2]);
		uint64_t constant = 0;
		uint8_t n_bytes = 0;
		if(!is_label) {
			if(reg == 15) {
				printf("error attempting to move a constant into R15 on line %d.\n", line_num);
				return 1;
			}
			constant = get_constant(tokens[2], &n_bytes, 0);
			if(n_bytes == 0) {
				printf("invalid constant or undefined reference to %s on line %d.\n", tokens[2], line_num);
				return 1;
			}
		} else n_bytes = 8;
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 

		if(is_label)
			add_label_ref(reg == current_sreg, tokens[2], output_size);
		else {		// after the move instruction, add the constant
			if(reg == current_sreg) add_8(0xC8+n_bytes-1); // add move to secondary
			else add_8(0xC0+n_bytes-1); // add move to primary
			if(n_bytes == 1) add_8((uint8_t)constant);
			if(n_bytes == 2) add_16((uint16_t)constant);
			if(n_bytes == 4) add_32((uint32_t)constant);
			if(n_bytes == 8) add_64((uint64_t)constant);
		}
	} else if(compstr(tokens[0], "BSWAPH")) { // BSWAP Rn
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 
		if(reg == current_preg) add_8(0xD0+1);
		else add_8(0xD8+1);
	} else if(compstr(tokens[0], "BSWAP")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 
		if(reg == current_preg) add_8(0xD0+3);
		else add_8(0xD8+3);
	} else if(compstr(tokens[0], "BSWAPD")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 
		if(reg == current_preg) add_8(0xD0+7);
		else add_8(0xD8+7);
	} else if(compstr(tokens[0], "NEG")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg);
		if(reg == current_preg) add_8(0x10);
		else add_8(0x11);
	} else if(compstr(tokens[0], "ADD")) { // IADD p=Rn s=Rn o=Rn
		READ_LINE_3_REGS;
		add_8(0x80); // add 32-bit integer add
	} else if(compstr(tokens[0], "SUB")) { // ISUB p=Rn s=Rn o=Rn
		READ_LINE_3_REGS;
		add_8(0x81); // add 32-bit integer subtraction
	} else if(compstr(tokens[0], "MUL")) {
		READ_LINE_3_REGS;
		add_8(0x82); // add 32-bit integer multiply
	} else if(compstr(tokens[0], "DIV")) {
		READ_LINE_3_REGS;
		add_8(0x83); // add 32-bit signed integer division
	} else if(compstr(tokens[0], "UDIV")) {
		READ_LINE_3_REGS;
		add_8(0x84); // add 32-bit unsigned integer division
	} else if(compstr(tokens[0], "MOD")) {
		READ_LINE_3_REGS;
		add_8(0x85); // add 32-bit signed integer division remainder
	} else if(compstr(tokens[0], "ADDD")) { // ADD p=Rn s=Rn o=Rn
		READ_LINE_3_REGS;
		add_8(0x86); // add 64-bit integer add
	} else if(compstr(tokens[0], "SUBD")) { // SUB p=Rn s=Rn o=Rn
		READ_LINE_3_REGS;
		add_8(0x87); // add 64-bit integer subtraction
	} else if(compstr(tokens[0], "MULD")) {
		READ_LINE_3_REGS;
		add_8(0x88); // add 64-bit integer multiply
	} else if(compstr(tokens[0], "DIVD")) {
		READ_LINE_3_REGS;
		add_8(0x89); // add 64-bit signed integer division
	} else if(compstr(tokens[0], "UDIVD")) {
		READ_LINE_3_REGS;
		add_8(0x8A); // add 64-bit unsigned integer division
	} else if(compstr(tokens[0], "MODD")) {
		READ_LINE_3_REGS;
		add_8(0x8B); // add 64-bit signed integer division remainder
	} else if(compstr(tokens[0], "FADD")) {
		READ_LINE_3_REGS;
		add_8(0x8C); // add 32-bit floating-point add
	} else if(compstr(tokens[0], "FSUB")) {
		READ_LINE_3_REGS;
		add_8(0x8D); // add 32-bit floating-point subtraction
	} else if(compstr(tokens[0], "FMUL")) {
		READ_LINE_3_REGS;
		add_8(0x8E); // add 32-bit floating-point multiplication
	} else if(compstr(tokens[0], "FDIV")) {
		READ_LINE_3_REGS;
		add_8(0x8F); // add 32-bit floating-point division
	} else if(compstr(tokens[0], "FPOW")) {
		READ_LINE_3_REGS;
		add_8(0x90); // add 32-bit floating-point exponentiation
	} else if(compstr(tokens[0], "DADD")) {
		READ_LINE_3_REGS;
		add_8(0x91); // add 64-bit floating-point add
	} else if(compstr(tokens[0], "DSUB")) {
		READ_LINE_3_REGS;
		add_8(0x92); // add 64-bit floating-point subtraction
	} else if(compstr(tokens[0], "DMUL")) {
		READ_LINE_3_REGS;
		add_8(0x93); // add 64-bit floating-point multiplication
	} else if(compstr(tokens[0], "DDIV")) {
		READ_LINE_3_REGS;
		add_8(0x94); // add 64-bit floating-point division
	} else if(compstr(tokens[0], "DPOW")) {
		READ_LINE_3_REGS;
		add_8(0x95); // add 64-bit floating-point exponentiation
	} else if(compstr(tokens[0], "CMP")) {
		READ_LINE_2_REGS;
		add_8(0x96); // add 32-bit integer compare
	} else if(compstr(tokens[0], "CMPD")) {
		READ_LINE_2_REGS;
		add_8(0x97); // add 64-bit integer compare
	} else if(compstr(tokens[0], "FCMP")) {
		READ_LINE_2_REGS;
		add_8(0x98); // add 32-bit floating-point compare
	} else if(compstr(tokens[0], "DCMP")) {
		READ_LINE_2_REGS;
		add_8(0x99); // add 64-bit floating-point compare
	} else if(compstr(tokens[0], "FTOD")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9A); // add 32-bit fp -> 64-bit fp
	} else if(compstr(tokens[0], "DTOF")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9B); // add 64-bit fp -> 32-bit fp
	} else if(compstr(tokens[0], "ITOF")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9C); // add 32-bit signed int -> 32-bit fp
	} else if(compstr(tokens[0], "ITOD")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9D); // add 64-bit signed int -> 64-bit fp
	} else if(compstr(tokens[0], "FTOI")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9E); // add 32-bit fp -> 32-bit signed int
	} else if(compstr(tokens[0], "DTOI")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x9F); // add 64-bit fp -> 64-bit signed int
	} else if(compstr(tokens[0], "LROT")) {
		READ_LINE_2_REGS;
		add_8(0x12); // add rotate left primary by number bits specified by secondary
	} else if(compstr(tokens[0], "RROT")) {
		READ_LINE_2_REGS;
		add_8(0x13); // add rotate right primary by number bits specified by secondary
	} else if(compstr(tokens[0], "LSH")) {
		READ_LINE_2_REGS;
		add_8(0x14); // add left shift primary by number bits specified by secondary
	} else if(compstr(tokens[0], "RSH")) {
		READ_LINE_2_REGS;
		add_8(0x16); // add right shift primary by number bits specified by secondary
	} else if(compstr(tokens[0], "ARSH")) {
		READ_LINE_2_REGS;
		add_8(0x18); // add arithmetic right shift primary by number bits specified by secondary
	} else if(compstr(tokens[0], "OR")) {
		READ_LINE_3_REGS;
		add_8(0x1A); // add bitwise OR
	} else if(compstr(tokens[0], "AND")) {
		READ_LINE_3_REGS;
		add_8(0x1B); // add bitwise AND
	} else if(compstr(tokens[0], "XOR")) {
		READ_LINE_3_REGS;
		add_8(0x1C); // add bitwise XOR
	} else if(compstr(tokens[0], "UMOD")) {
		READ_LINE_3_REGS;
		add_8(0x1D); // add 32-bit unsigned modulo
	} else if(compstr(tokens[0], "UMODD")) {
		READ_LINE_3_REGS;
		add_8(0x1E); // add 64-bit unsigned modulo
	} else if(compstr(tokens[0], "REGCOPY")) {
		READ_LINE_2_REGS;
		add_8(0x1F); // add copy primary to secondary
	} else if(compstr(tokens[0], "CLR")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 
		if(reg == current_preg) add_8(0x21); // add clear primary with 0s
		else add_8(0x22); // add clear secondary with 0s
	} else if(compstr(tokens[0], "FILL")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg && reg != current_sreg) add_primary_set(reg); 
		if(reg == current_preg) add_8(0x23); // add clear primary with 1s
		else add_8(0x24); // add clear secondary with 1s
	} else if(compstr(tokens[0], "NTHR")) { // new thread
		READ_LINE_3_REGS;
		add_8(0x25);
	} else if(compstr(tokens[0], "DTCH")) { // detach a thread
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x26);
	} else if(compstr(tokens[0], "DTHR")) { // destroy a thread
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x27);
	} else if(compstr(tokens[0], "JOIN")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x28);
	} else if(compstr(tokens[0], "SLEEP")) { // EXEC p=Rn o=Rn
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x29);
	} else if(compstr(tokens[0], "THRCTL")) { // thread control; query thread info/modify traits of a descendant
		READ_LINE_3_REGS;
		add_8(0x2A);
	} else if(compstr(tokens[0], "COND")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x2B);
	} else if(compstr(tokens[0], "JMP")) {
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x2C);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPEQ")) {
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x2D);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPNE")) { // not equal (integers) not equal or unordered (fp)
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x2E);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPCS")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x2F);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPCC")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x30);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPN")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x31);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPP")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x32);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPVS")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x33);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPVC")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x34);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPHI")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x35);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPLS")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x36);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPGE")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x37);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPLT")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x38);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPGT")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x39);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "JMPLE")) { 
		if(n_tokens != 2) return 1;
		if(!check_label(tokens[1]) && check_str_reg(tokens[1]) != 0) {
			printf("invalid register or undefined reference to %s on line %d.\n", tokens[1], line_num);
			return 1;
		}
		if(check_str_reg(tokens[1]) == 0) { // jump to address in register
			uint8_t reg = strtoull(tokens[1]+1,0,10);
			if(reg != current_preg) add_primary_set(reg);
		} else { // jump to address of label
			if(current_preg != 14) add_primary_set(14);
			add_label_ref(0, tokens[1], output_size);		// overwrite LR with label
		}
		add_8(0x3A);
		add_primary_set(current_preg);
		add_secondary_set(current_sreg);
		add_output_set(current_oreg);
	} else if(compstr(tokens[0], "RET")) {
		if(n_tokens != 1) return 1;
		add_8(0x3B);
	} else if(compstr(tokens[0], "FCTL")) {
		READ_LINE_3_REGS;
		add_8(0x3C);
	} else if(compstr(tokens[0], "FDEL")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x3D);
	} else if(compstr(tokens[0], "FCLOSE")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x3E);
	} else if(compstr(tokens[0], "FSET")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x3F);
	} else if(compstr(tokens[0], "FWRITE")) {
		READ_LINE_3_REGS;
		add_8(0x40);
	} else if(compstr(tokens[0], "FREAD")) {
		READ_LINE_3_REGS;
		add_8(0x41);
	} else if(compstr(tokens[0], "FSIZE")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x42);
	} else if(compstr(tokens[0], "NDRVS")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_oreg) add_output_set(reg);
		add_8(0x43);
	} else if(compstr(tokens[0], "LDRVS")) {
		READ_LINE_2_REGS;
		add_8(0x44);
	} else if(compstr(tokens[0], "FLIST")) {
		READ_LINE_2_REGS;
		add_8(0x45);
	} else if(compstr(tokens[0], "FLSIZE")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x46);
	} else if(compstr(tokens[0], "DRVSET")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x47);
	} else if(compstr(tokens[0], "GEN")) {
		READ_LINE_3_REGS;
		add_8(0x48);
	} else if(compstr(tokens[0], "DEL")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x49);
	} else if(compstr(tokens[0], "BIND")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x4A);
	} else if(compstr(tokens[0], "BFBO")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x4B);
	} else if(compstr(tokens[0], "BDSC")) {
		READ_LINE_2_REGS;
		add_8(0x4C);
	} else if(compstr(tokens[0], "BPIPE")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x4D);
	} else if(compstr(tokens[0], "UDSC")) {
		READ_LINE_2_REGS;
		add_8(0x4E);
	} else if(compstr(tokens[0], "BSVI")) {
		READ_LINE_2_REGS;
		add_8(0x4F);
	} else if(compstr(tokens[0], "SIZE")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x50);
	} else if(compstr(tokens[0], "MAP")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x51);
	} else if(compstr(tokens[0], "BALLOC")) {
		READ_LINE_2_REGS;
		add_8(0x52);
	} else if(compstr(tokens[0], "UTEX")) {
		READ_LINE_2_REGS;
		add_8(0x53);
	} else if(compstr(tokens[0], "GMIPS")) {
		if(n_tokens != 1) return 1;
		add_8(0x54);
	} else if(compstr(tokens[0], "ATTACH")) {
		READ_LINE_2_REGS;
		add_8(0x55);
	} else if(compstr(tokens[0], "CBUFF")) {
		READ_LINE_2_REGS;
		add_8(0x56);
	} else if(compstr(tokens[0], "UACCEL")) {
		READ_LINE_2_REGS;
		add_8(0x57);
	} else if(compstr(tokens[0], "RCMD")) {
		if(n_tokens != 1) return 1;
		add_8(0x58);
	} else if(compstr(tokens[0], "GSUBMIT")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x59);
	} else if(compstr(tokens[0], "CSUBMIT")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x5A);
	} else if(compstr(tokens[0], "FCMDS")) {
		if(n_tokens != 1) return 1;
		add_8(0x5B);
	} else if(compstr(tokens[0], "DRAW")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x5C);
	} else if(compstr(tokens[0], "IDRAW")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x5D);
	} else if(compstr(tokens[0], "BUPDATE")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x5E);
	} else if(compstr(tokens[0], "PUSHC")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x5F);
	} else if(compstr(tokens[0], "TRACE")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x60);
	} else if(compstr(tokens[0], "ASCPY")) {
		READ_LINE_2_REGS;
		add_8(0x61);
	} else if(compstr(tokens[0], "SWAP")) {
		if(n_tokens != 1) return 1;
		add_8(0x62);
	} else if(compstr(tokens[0], "DSET")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x63);
	} else if(compstr(tokens[0], "STXMOD")) {
		READ_LINE_2_REGS;
		add_8(0x64);
	} else if(compstr(tokens[0], "DSPCMP")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x65);
	} else if(compstr(tokens[0], "GETBND")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_oreg) add_output_set(reg);
		add_8(0x66);
	} else if(compstr(tokens[0], "GETHWI")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_oreg) add_output_set(reg);
		add_8(0x66);
	} else if(compstr(tokens[0], "USEG")) {
		READ_LINE_3_REGS;
		add_8(0x67);
	} else if(compstr(tokens[0], "CLR7")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x68);
	} else if(compstr(tokens[0], "CLR6")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x69);
	} else if(compstr(tokens[0], "CLR4")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6A);
	} else if(compstr(tokens[0], "SEXT7")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6B);
	} else if(compstr(tokens[0], "SEXT6")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6C);
	} else if(compstr(tokens[0], "SEXT4")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6D);
	} else if(compstr(tokens[0], "BITN")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6E);
	} else if(compstr(tokens[0], "FNEG")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x6F);
	} else if(compstr(tokens[0], "DNEG")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x70);
	} else if(compstr(tokens[0], "INCR")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x71);
	} else if(compstr(tokens[0], "DECR")) {
		if(n_tokens != 2) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		uint8_t reg = strtoull(tokens[1]+1,0,10);
		if(reg != current_preg) add_primary_set(reg);
		add_8(0x72);
	} else if(compstr(tokens[0], "FMOD")) {
		READ_LINE_3_REGS;
		add_8(0x73);
	} else if(compstr(tokens[0], "DMOD")) {
		READ_LINE_3_REGS;
		add_8(0x74);
	} else if(compstr(tokens[0], "MATHF")) {
		READ_LINE_3_REGS;
		add_8(0x75);
	} else if(compstr(tokens[0], "DCAPT")) {
		READ_LINE_2_REGS;
		add_8(0x76);
	} else if(compstr(tokens[0], "TIME")) {
		if(n_tokens != 3) return 1;
		if(check_str_reg(tokens[1]) != 0) return 1;
		if(check_str_reg(tokens[2]) != 0) return 1;
		uint8_t preg = strtoull(tokens[1]+1,0,10);
		uint8_t oreg = strtoull(tokens[2]+1,0,10);
		if(preg != current_preg) add_primary_set(preg);
		if(oreg != current_oreg) add_output_set(oreg);
		add_8(0x77);
	} else if(compstr(tokens[0], "MCOPY")) {
		READ_LINE_3_REGS;
		add_8(0x78);
	} else if(compstr(tokens[0], "ASLCTL")) {
		READ_LINE_3_REGS;
		add_8(0x79);
	} else if(compstr(tokens[0], "ADFCTL")) {
		READ_LINE_3_REGS;
		add_8(0x7A);
	} else if(compstr(tokens[0], "VDFCTL")) {
		READ_LINE_3_REGS;
		add_8(0x7B);
	} else if(compstr(tokens[0], "NETCTL")) {
		READ_LINE_2_REGS;
		add_8(0x7C);
	} else if(compstr(tokens[0], "LLVEC")) {
		READ_LINE_2_REGS;
		add_8(0x7D);
	} else if(compstr(tokens[0], "LRVEC")) {
		READ_LINE_2_REGS;
		add_8(0x7E);
	} else if(compstr(tokens[0], "SIMD")) {
		READ_LINE_2_REGS;
		add_8(0x7F);
	} else return 1; // if reached here, line was invalid
}
	return 0;
}

// returns 1 on error, 0 otherwise
uint8_t read_file(char* asm_file) {
	FILE* afile = fopen(asm_file,"r");
	if(!afile) { printf("error opening assembly file \"%s\"; check that it exists and spelling is correct.\n", asm_file); return 1; }
	uint64_t file_size = 0;
	fseek(afile, 0, SEEK_END);
	file_size = ftell(afile);
	fseek(afile, 0, 0);
	if(file_size == 0) { printf("assembly file \"%s\" has size of 0; exiting.\n", asm_file); return 1; }
	char line[5000];
	uint32_t line_n = 1;
	// search for label definitions and add them (so that references to them can be made) using add_label
	while(fgets(line,file_size+1,afile) != 0) {
		char** tokens = 0;
		uint32_t n_tokens = 0;
		int64_t token_start = -1; // where the current token started; -1 if position in line not on token
		uint8_t n_colons = 0;
		for(uint32_t i = 0;; i++) {
			uint8_t token_char = (line[i] != ' ') && (line[i] != '\t') && (line[i] != '\n') && (line[i] != '\0') && (line[i] != ';');
			if(token_start == -1 && token_char) token_start = i; // start token
			if(token_start != -1 && !token_char) { // end token
				tokens = realloc(tokens, sizeof(char*)*(n_tokens+1));
				tokens[n_tokens] = malloc((i-token_start)+1);	// what is the number of bytes to allocate? (take into account the null character)
				memcpy(tokens[n_tokens], &line[token_start], (i-token_start)+1);
				tokens[n_tokens][i-token_start] = '\0';
				token_start = -1;
				n_tokens++;
			}
			if(line[i] == ':') n_colons++;
			if(line[i] == ':' && i == 0) return 1; // missing label name
			if(line[i] == '\n' || line[i] == '\0' || line[i] == ';') break;
		}
		if(n_tokens == 0) continue; // nothing on this line to add; return without error
		if(n_colons > 1) return 1;
		if(n_colons && n_tokens == 1) {
			tokens[0][strlen(tokens[0])-1] = '\0';
			add_label(tokens[0]);
		}
	}
	fseek(afile, 0, 0);
	while(fgets(line,file_size+1,afile) != 0) {
		if(process_line(line,line_n) != 0) { printf("error on line %d; exiting.\n%s\n", line_n, line); return 1; }
		line_n++;
	}
	if(fclose(afile)) { printf("error on closing assembly file \"%s\"; exiting.\n", asm_file); return 1; }

	//
	//			PROCESS LABEL REFERENCES
	//

	for(int32_t i = 0; view_unused_labels && i < n_labels; i++)
		if(label_n_refs[i] == 0) printf("warning: no references to label %s found in program\n", label_names[i]);

	if(!n_label_refs || !n_labels)
		return 0;

	// need 2 steps:
	// 1. calculate new label addresses, pretending that each label reference has been expanded
	// 2. actually insert label references (create a copy of output)

	uint32_t label_addr_incr[n_labels];			// how much to increase each label's address by
	memset(label_addr_incr, 0, sizeof(uint32_t)*n_labels);

	uint8_t label_ref_sizes[n_labels];			// minimum label reference size (in bytes) for each label
	memset(label_ref_sizes, 2, n_labels);		// label reference size is at least 2 (1 move instruction + 1 byte for immediate value)
		// check each label. If all references of label i preceding label i are big enough to fit the label's address + all preceding references,
		// then continue. otherwise, increase the current label's ref size and start again from the first label.
	for(int32_t i = 0; i < n_labels; i++) {
		uint32_t preceding_data = 0;

		for(uint32_t j = 0; j < n_label_refs && label_ref_addr[j] <= label_addresses[i]; j++)
			preceding_data += label_ref_sizes[label_ref_ids[j]];

		if(get_value_size(label_addresses[i]+preceding_data)+1 > label_ref_sizes[i]) {
			label_ref_sizes[i] = 2*(label_ref_sizes[i] - 1) + 1;
			memset(label_addr_incr, 0, sizeof(uint32_t)*n_labels);
			i = -1;
			continue;
		} else label_addr_incr[i] = preceding_data;
	}

	for(uint32_t i = 0; i < n_labels; i++)
		label_addresses[i] += label_addr_incr[i];

	// now we know the sizes of each label's references, and the updated addresses of the labels. just need to insert the references!
	uint32_t new_size = output_size;
	for(uint32_t i = 0; i < n_labels; i++)
		new_size += label_n_refs[i] * label_ref_sizes[i];

	uint8_t* old_out = output;
	uint32_t old_size = output_size;
	output = malloc(new_size);
	output_size = new_size;

	uint32_t p = 0, offset = 0;
	for(int32_t i = 0; i < n_label_refs; i++) {
		// set ref at output[ref_addr + offset]
		uint8_t ref[9], ref_size = label_ref_sizes[label_ref_ids[i]];
		memset(ref, 0, 9);
		ref[0] = label_ref_regs[i] == 0 ? 0xC0+ref_size-2 : 0xC8+ref_size-2;
		memcpy(ref + 1, &label_addresses[label_ref_ids[i]], ref_size-1);
		memcpy(output + label_ref_addr[i] + offset, ref, ref_size);

		// copy all data preceding the reference
		int32_t end = label_ref_addr[i]-1;
		if(end > 0) memcpy(output + p + offset, old_out + p, end - p + 1);

		offset += ref_size;
		p = label_ref_addr[i];
	}
	if(p < old_size-1)		// copy the data that follows the last reference
		memcpy(output + p + offset, old_out + p, old_size - p);

	return 0;
}

void write_file(char* out_file) {
	FILE* ofile = fopen(out_file,"w");
	if(!ofile) { printf("error opening output file \"%s\"; check that it exists and spelling is correct.\n", out_file); return; }
	if(output_size == 0) { printf("there was no assembler output; exiting.\n"); return; }
	fwrite(output, 1, output_size, ofile);
	if(fclose(ofile)) { printf("error on closing output file \"%s\"; exiting.\n", out_file); return; }
}

void write_shader_binaries() {
	for(uint32_t i = 0; i < n_shader_regions; i++) {
		uint32_t n_bytes = shader_region_ends[i]-shader_region_starts[i]+1;
		if(!n_bytes) continue;
		char shader_id[30];
		sprintf(shader_id, "%u", i);
		char* filename = calloc(1,strlen("_shader")+strlen(shader_id)+1);
		strcpy(filename, "_shader");
		strcpy(filename+strlen("_shader"), shader_id);
		FILE* ofile = fopen(filename,"w");
		if(!ofile) { printf("could not open file \"%s\" for shader output.\n", filename); continue; }
		fwrite(&output[shader_region_starts[i]], 1, n_bytes, ofile);
		if(fclose(ofile)) { printf("error on closing shader output file \"%s\".\n", filename); continue; }
	}
}

int main(int argc, char* argv[]) {
	if(argc < 2 || argc > 4) { printf("incorrect number of arguments (expected format: ./assembler file [-v] [-s]), exiting.\n"); return 1; }
	for(uint32_t i = 2; i < argc; i++)
		if(strcmp(argv[i], "-v") == 0) view_unused_labels = 1;
		else if(strcmp(argv[i], "-s") == 0) output_shader_binaries = 1;
		else {
			printf("invalid command argument %s (expected format: ./assembler file [-v] [-s]), exiting.\n", argv[i]);
			return 1;
		}
	if(read_file(argv[1]) != 0) return 1;
	write_file("out.bin");
	if(output_shader_binaries) write_shader_binaries();
	return 0;
}
