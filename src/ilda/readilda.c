/* readilda.c An external for Pure Data that reads and writes binary files
*	Copyright (C) 2007  Martin Peach
*
*	This program is free software; you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation; either version 2 of the License, or
*	any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program; if not, write to the Free Software
*	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
*	based on binfile by martin.peach@sympatico.ca
*/

/*
 * readilda : read ilda files, part of ilda librery
 * Antoine Villeret - EnsadLab 2013, antoine.villeret[AT]gmail.com
 * 
 */ 
#include "m_pd.h"
#include <stdio.h>
#include <string.h>

static t_class *readilda_class;

#define ALLOC_BLOCK_SIZE 65536 /* number of bytes to add when resizing buffer */
#define PATH_BUF_SIZE 1024 /* maximumn length of a file path */

typedef struct t_readilda
{
    t_object    x_obj;
    t_outlet    *x_bin_outlet;
    t_outlet    *x_info_outlet;
    t_outlet    *x_bang_outlet;
    FILE        *x_fP;
    t_symbol    *x_our_directory;
    char        x_fPath[PATH_BUF_SIZE];
    char        *x_buf; /* read/write buffer in memory for file contents */
    size_t      x_buf_length; /* current length of buf */
    size_t      x_length; /* current length of valid data in buf */
    size_t      x_rd_offset; /* current read offset into the buffer */
    size_t      x_wr_offset; /* current write offset into the buffer */
} t_readilda;

static void readilda_rewind (t_readilda *x);
static void readilda_free(t_readilda *x);
static FILE *readilda_open_path(t_readilda *x, char *path, char *mode);
static void readilda_read(t_readilda *x, t_symbol *path);
static void readilda_write(t_readilda *x, t_symbol *path);
static void readilda_bang(t_readilda *x);
static void readilda_float(t_readilda *x, t_float val);
static void readilda_list(t_readilda *x, t_symbol *s, int argc, t_atom *argv);
static void readilda_add(t_readilda *x, t_symbol *s, int argc, t_atom *argv);
static void readilda_clear(t_readilda *x);
static void readilda_info(t_readilda *x);
static void readilda_set(t_readilda *x, t_symbol *s, int argc, t_atom *argv);
static void readilda_set_read_index(t_readilda *x, t_float offset);
static void readilda_set_write_index(t_readilda *x, t_float offset);
static void *readilda_new(t_symbol *s, int argc, t_atom *argv);
void readilda_setup(void);

void readilda_setup(void)
{
    readilda_class = class_new (gensym("readilda"),
        (t_newmethod) readilda_new,
        (t_method)readilda_free, sizeof(t_readilda),
        CLASS_DEFAULT,
        A_GIMME, 0);

    class_addbang(readilda_class, readilda_bang);
    class_addfloat(readilda_class, readilda_float);
    class_addlist(readilda_class, readilda_list);
    class_addmethod(readilda_class, (t_method)readilda_read, gensym("read"), A_DEFSYMBOL, 0);
    class_addmethod(readilda_class, (t_method)readilda_write, gensym("write"), A_DEFSYMBOL, 0);
    class_addmethod(readilda_class, (t_method)readilda_add, gensym("add"), A_GIMME, 0);
    class_addmethod(readilda_class, (t_method)readilda_set, gensym("set"), A_GIMME, 0);
    class_addmethod(readilda_class, (t_method)readilda_set_read_index, gensym("readat"), A_DEFFLOAT, 0);
    class_addmethod(readilda_class, (t_method)readilda_set_write_index, gensym("writeat"), A_DEFFLOAT, 0);
    class_addmethod(readilda_class, (t_method)readilda_clear, gensym("clear"), 0);
    class_addmethod(readilda_class, (t_method)readilda_rewind, gensym("rewind"), 0);
    class_addmethod(readilda_class, (t_method)readilda_info, gensym("info"), 0);
}

static void *readilda_new(t_symbol *s, int argc, t_atom *argv)
{
    t_readilda  *x = (t_readilda *)pd_new(readilda_class);
    t_symbol    *pathSymbol;
    int         i;

    if (x == NULL)
    {
        error("readilda: Could not create...");
        return x;
    }
    x->x_fP = NULL;
    x->x_fPath[0] = '\0';
    x->x_our_directory = canvas_getcurrentdir();/* get the current directory to use as the base for relative file paths */
    x->x_buf_length = ALLOC_BLOCK_SIZE;
    x->x_rd_offset = x->x_wr_offset = x->x_length = 0L;
    /* find the first string in the arg list and interpret it as a path to a file */
    for (i = 0; i < argc; ++i)
    {
        if (argv[i].a_type == A_SYMBOL)
        {
            pathSymbol = atom_getsymbol(&argv[i]);
            if (pathSymbol != NULL)
                readilda_read(x, pathSymbol);
        }
    }
    /* find the first float in the arg list and interpret it as the size of the buffer */
    for (i = 0; i < argc; ++i)
    {
        if (argv[i].a_type == A_FLOAT)
        {
            x->x_buf_length = atom_getfloat(&argv[i]);
            break;
        }
    }
    if ((x->x_buf = getbytes(x->x_buf_length)) == NULL)
        error ("readilda: Unable to allocate %lu bytes for buffer", x->x_buf_length);
    x->x_bin_outlet = outlet_new(&x->x_obj, gensym("float"));
    x->x_info_outlet = outlet_new(&x->x_obj, gensym("list"));
    x->x_bang_outlet = outlet_new(&x->x_obj, gensym("bang")); /* bang at end of file */
    return (void *)x;
}

static void readilda_free(t_readilda *x)
{
    if (x->x_buf != NULL)
        freebytes(x->x_buf, x->x_buf_length);
    x->x_buf = NULL;
    x->x_buf_length = 0L;
}

static FILE *readilda_open_path(t_readilda *x, char *path, char *mode)
/* path is a string. Up to PATH_BUF_SIZE-1 characters will be copied into x->x_fPath. */
/* mode should be "rb" or "wb" */
/* x->x_fPath will be used as a file name to open. */
/* readilda_open_path attempts to open the file for binary mode reading. */
/* Returns FILE pointer if successful, else 0. */
{
    FILE    *fP = NULL;
    char    tryPath[PATH_BUF_SIZE];
    char    slash[] = "/";

    /* If the first character of the path is a slash then the path is absolute */
    /* On MSW if the second character of the path is a colon then the path is absolute */
    if ((path[0] == '/') || (path[0] == '\\') || (path[1] == ':'))
    {
        strncpy(tryPath, path, PATH_BUF_SIZE-1); /* copy path into a length-limited buffer */
        /* ...if it doesn't work we won't mess up x->fPath */
        tryPath[PATH_BUF_SIZE-1] = '\0'; /* just make sure there is a null termination */
        fP = fopen(tryPath, mode);
    }
    if (fP == NULL)
    {
        /* Then try to open the path from the current directory */
        strncpy(tryPath, x->x_our_directory->s_name, PATH_BUF_SIZE-1); /* copy directory into a length-limited buffer */
        strncat(tryPath, slash, PATH_BUF_SIZE-1); /* append path to a length-limited buffer */
        strncat(tryPath, path, PATH_BUF_SIZE-1); /* append path to a length-limited buffer */
        /* ...if it doesn't work we won't mess up x->fPath */
        tryPath[PATH_BUF_SIZE-1] = '\0'; /* make sure there is a null termination */
        fP = fopen(tryPath, mode);
    }
    if (fP != NULL)
        strncpy(x->x_fPath, tryPath, PATH_BUF_SIZE);
    else
        x->x_fPath[0] = '\0';
    return fP;
}

static void readilda_write(t_readilda *x, t_symbol *path)
/* open the file for writing and write the entire buffer to it, then close it */
{
    size_t bytes_written = 0L;

    if (0==(x->x_fP = readilda_open_path(x, path->s_name, "wb")))
        error("readilda: Unable to open %s for writing", path->s_name);
    bytes_written = fwrite(x->x_buf, 1L, x->x_length, x->x_fP);
    if (bytes_written != x->x_length) post("readilda: %ld bytes written != %ld", bytes_written, x->x_length);
    else post("readilda: wrote %ld bytes to %s", bytes_written, path->s_name);
    fclose(x->x_fP);
    x->x_fP = NULL;
}

static void readilda_read(t_readilda *x, t_symbol *path)
/* open the file for reading and load it into the buffer, then close it */
{
    size_t file_length = 0L;
    size_t bytes_read = 0L;

    if (0==(x->x_fP = readilda_open_path(x, path->s_name, "rb")))
    {
        error("readilda: Unable to open %s for reading", path->s_name);
        return;
    }
    /* get length of file */
    while (EOF != getc(x->x_fP)) ++file_length;

    if (file_length == 0L) return;
    /* get storage for file contents */
    if (0 != x->x_buf) freebytes(x->x_buf, x->x_buf_length);
    x->x_buf = getbytes(file_length);
    if (NULL == x->x_buf)
    {
        x->x_buf_length = 0L;
        error ("readilda: unable to allocate %ld bytes for %s", file_length, path->s_name);
        return;
    }
    x->x_rd_offset = 0L;
    /* read file into buf */
    rewind(x->x_fP);
    bytes_read = fread(x->x_buf, 1L, file_length, x->x_fP);
    x->x_buf_length = bytes_read;
    x->x_wr_offset = x->x_buf_length; /* write new data at end of file */
    x->x_length = x->x_buf_length; /* file length is same as buffer size 7*/
    x->x_rd_offset = 0L; /* read from start of file */
    fclose (x->x_fP);
    x->x_fP = NULL;
    if (bytes_read != file_length) post("readilda length %ld not equal to bytes read (%ld)", file_length, bytes_read);
    else post("binfle: read %ld bytes from %s", bytes_read, path->s_name);
    
    int i;
    for ( i=0 ; i<x->x_buf_length-4 ; i++ ){
        if ( x->x_buf[i] == 'I' && x->x_buf[i+1] == 'L' && x->x_buf[i+2] == 'D' && x->x_buf[i+3] == 'A' )
        {
            unsigned long int format = x->x_buf[i+4] << 24 + x->x_buf[i+5] << 16 + x->x_buf[i+6] << 8 + x->x_buf[i+7];
            post("found a frame at %d, format : %ld", i, format);
            i+=7;
        } else {
            i+=4;
        }
    }
}

static void readilda_bang(t_readilda *x)
/* get the next byte in the file and send it out x_bin_list_outlet */
{
    unsigned char c;

    if (x->x_rd_offset < x->x_length)
    {
        c = x->x_buf[x->x_rd_offset++];
        outlet_float(x->x_bin_outlet, (float)c);
    }
    else outlet_bang(x->x_bang_outlet);
}

/* The arguments of the ``list''-method
* a pointer to the class-dataspace
* a pointer to the selector-symbol (always &s_list)
* the number of atoms and a pointer to the list of atoms:
*/

static void readilda_add(t_readilda *x, t_symbol *s, int argc, t_atom *argv)
/* add a list of bytes to the buffer */
{
    int         i, j;
    float       f;

    for (i = 0; i < argc; ++i)
    {
        if (A_FLOAT == argv[i].a_type)
        {
            j = atom_getint(&argv[i]);
            f = atom_getfloat(&argv[i]);
            if (j < -128 || j > 255)
            {
                error("readilda: input (%d) out of range [0..255]", j);
                return;
            }
            if (j != f)
            {
                error("readilda: input (%f) not an integer", f);
                return;
            }
            if (x->x_buf_length <= x->x_wr_offset)
            {
                x->x_buf = resizebytes(x->x_buf, x->x_buf_length, x->x_buf_length+ALLOC_BLOCK_SIZE);
                if (x->x_buf == NULL)
                {
                    error("readilda: unable to resize buffer");
                    return;
                }
                x->x_buf_length += ALLOC_BLOCK_SIZE;
            }
            x->x_buf[x->x_wr_offset++] = j;
            if (x->x_length < x->x_wr_offset) x->x_length = x->x_wr_offset;
        }
        else
        {
            error("readilda: input %d not a float", i);
            return;
        }
    }
}

static void readilda_list(t_readilda *x, t_symbol *s, int argc, t_atom *argv)
{
    readilda_add(x, s, argc, argv);
}

static void readilda_set(t_readilda *x, t_symbol *s, int argc, t_atom *argv)
/* clear then add a list of bytes to the buffer */
{
    readilda_clear(x);
    readilda_add(x, s, argc, argv);
}

static void readilda_set_read_index(t_readilda *x, t_float offset)
/* set the read offset, always < length */
{
    size_t intoffset = offset;

    if (intoffset < x->x_length) x->x_rd_offset = intoffset;
    else if (x->x_length > 0) x->x_rd_offset = x->x_length-1;
    else x->x_rd_offset = 0L;
}

static void readilda_set_write_index(t_readilda *x, t_float offset)
/* set the write offset, always <= length */
{
    size_t intoffset = offset;

    if (intoffset <= x->x_length) x->x_wr_offset = intoffset;
    else  x->x_wr_offset = x->x_length;
}

static void readilda_clear(t_readilda *x)
{
    x->x_wr_offset = 0L;
    x->x_rd_offset = 0L;
    x->x_length = 0L;
}

static void readilda_float(t_readilda *x, t_float val)
/* add a single byte to the file */
{
    t_atom a;

    SETFLOAT(&a, val);
    readilda_add(x, gensym("float"), 1, &a);
}

static void readilda_rewind (t_readilda *x)
{
    x->x_rd_offset = 0L;
}

static void readilda_info(t_readilda *x)
{
    t_atom *output_atom = getbytes(sizeof(t_atom));
    SETFLOAT(output_atom, x->x_buf_length);
    outlet_anything( x->x_info_outlet, gensym("buflength"), 1, output_atom);
    SETFLOAT(output_atom, x->x_length);
    outlet_anything( x->x_info_outlet, gensym("length"), 1, output_atom);
    SETFLOAT(output_atom, x->x_rd_offset);
    outlet_anything( x->x_info_outlet, gensym("readoffset"), 1, output_atom);
    SETFLOAT(output_atom, x->x_wr_offset);
    outlet_anything( x->x_info_outlet, gensym("writeoffset"), 1, output_atom);
    freebytes(output_atom,sizeof(t_atom));
}
/* fin readilda.c */
