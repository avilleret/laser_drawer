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
#include "ilda.h"

static t_class *readilda_class;

#define ALLOC_BLOCK_SIZE 65536 /* number of bytes to add when resizing buffer */
#define PATH_BUF_SIZE 1024 /* maximumn length of a file path */

#define PT_COORD_MAX 32767.5
#define CH_X 0
#define CH_Y 1
#define CH_R 2
#define CH_G 3
#define CH_B 4

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

    t_ilda_frame frame[128]; /* actually we can't store more than 128 frame, this is arbitraty */
    unsigned int frame_count;
    t_ilda_channel channel[5];

} t_readilda;

static void readilda_free(t_readilda *x);
static FILE *readilda_open_path(t_readilda *x, char *path, char *mode);
static void readilda_read(t_readilda *x, t_symbol *path);
static void readilda_write(t_readilda *x, t_symbol *path);
static void readilda_bang(t_readilda *x);
static void readilda_float(t_readilda *x, t_float val);
static void readilda_list(t_readilda *x, t_symbol *s, int argc, t_atom *argv);
static void readilda_add(t_readilda *x, t_symbol *s, int argc, t_atom *argv);
static void readilda_clear(t_readilda *x);
static void readilda_settab ( t_readilda *x, t_symbol* s, unsigned int ac, t_atom* av );
static void *readilda_new(t_symbol *s, int argc, t_atom *argv);
static void decode_format_0(t_readilda* x, unsigned int index);
static void decode_format_1(t_readilda* x, unsigned int index);
static void decode_format_2(t_readilda* x, unsigned int index);
static void decode_format_3(t_readilda* x, unsigned int index);
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
    class_addmethod(readilda_class, (t_method)readilda_clear, gensym("clear"), 0);
    class_addmethod(readilda_class, (t_method) readilda_settab, gensym("settab"), A_GIMME, 0);
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
    x->x_info_outlet = outlet_new(&x->x_obj, gensym("info"));
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
    
    unsigned int i,j=0;
    x->frame_count=0;
    for ( i=0 ; i<x->x_buf_length-4 ; ){
        if ( x->x_buf[i] == 'I' && x->x_buf[i+1] == 'L' && x->x_buf[i+2] == 'D' && x->x_buf[i+3] == 'A' )
        {
            unsigned long int format = (x->x_buf[i+4] << 24) + (x->x_buf[i+5] << 16) + (x->x_buf[i+6] << 8) + x->x_buf[i+7];
            x->frame[j].format=format;
            
            if ( format < 2 ){
                //~ this is a 2D or 3D frame, header is the same
                int k = i;
                char name[9]={x->x_buf[k],x->x_buf[k+1],x->x_buf[k+2],x->x_buf[k+3],x->x_buf[k+4],x->x_buf[k+5],x->x_buf[k+6],x->x_buf[k+7],'\0'};
                x->frame[j].frame_name=gensym(name);
                k = i+8;
                char compagny_name[9]={x->x_buf[k],x->x_buf[k+1],x->x_buf[k+2],x->x_buf[k+3],x->x_buf[k+4],x->x_buf[k+5],x->x_buf[k+6],x->x_buf[k+7],'\0'};
                x->frame[j].compagny_name=gensym(compagny_name);
                
                x->frame[j].point_number=(float) (x->x_buf[i+24] << 8) + x->x_buf[i+25];
                x->frame[j].frame_number=(float) (x->x_buf[i+26] << 8) + x->x_buf[i+27];
                x->frame[j].total_frame=(float) (x->x_buf[i+28] << 8) + x->x_buf[i+29];
                x->frame[j].scanner=(float) x->x_buf[i+29];
                
                x->frame[j].start_id=i;
                i+=32;
                j++;
                x->frame_count=j;
            } else if ( format == 2 ){
                //~ this is a Color Lookup Table Frame
            } else if ( format == 3 ){
                //~ this is a True Color Frame
            }
        } else {
            i++;
        }
           
    }
    for ( i=0;i<x->frame_count;i++){

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

static void readilda_clear(t_readilda *x)
{
    x->x_wr_offset = 0L;
    x->x_rd_offset = 0L;
    x->x_length = 0L;
    x->frame_count=0;
}

static void readilda_float(t_readilda *x, t_float val)
//~ retrieve given frame
{
    unsigned int index = (int) val;
    
    if ( index > x->frame_count-1 ){
        return;
    }  
    
    if ( !x->channel[0].array || !x->channel[1].array || !x->channel[2].array || !x->channel[3].array || !x->channel[4].array ){
        error("you should specify x, y, r, g and b before");
        return;
    }

    //~ fill the tables
    switch (x->frame[index].format){
        case 0:
            decode_format_0(x,index);
            break;
        case 1:
            decode_format_1(x,index);
            break;
        case 2:
            decode_format_2(x,index);
            break;
        case 3:
            decode_format_3(x,index);
            break;
        }
    }

static void readilda_settab ( t_readilda *x, t_symbol* s, unsigned int ac, t_atom* av )
{
    if (ac%2 != 0 ) {
		error("wong arg number, usage : settab <x|y|r|g|b> <table name>");
		return;
	}
    unsigned int i=0;
	for(i=0;i<ac;i++){
        if ( av[i].a_type != A_SYMBOL){
            error("wrong arg type, settab args must be symbol");
            return;
        }
	}
    
    for(i=0;i<ac/2;i++){
        t_garray *tmp_array;
        tmp_array = NULL;
        tmp_array = (t_garray *)pd_findbyclass(av[2*i+1].a_w.w_symbol, garray_class);
        if (tmp_array){
            int index=0;
            if ( strcmp( av[2*i].a_w.w_symbol->s_name, "x") == 0){
                index=0;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "y") == 0){
                index=1;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "r") == 0){
                index=2;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "g") == 0){
                index=3;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "b") == 0){
                index=4;
            }
            
            x->channel[index].array=tmp_array;
            x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
        } else {
            error("%s: no such array", av[2*i+1].a_w.w_symbol->s_name);           
        }
    } 
}

static void decode_format_0(t_readilda* x, unsigned int index){
   
   int size=x->frame[index].point_number;
   
   if ( size == 0 ) return; 
    
    //~ resize tables
    int i;
    for ( i=0; i<5; i++ ){
        if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
            error("%s: bad template", x->channel[i].arrayname->s_name);
            return;
        } else if ( x->channel[i].vecsize != size ){
            garray_resize_long(x->channel[i].array,size);
            if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
                error("%s: can't resize correctly", x->channel[i].arrayname->s_name);
                return;
            } 
        }
    }
    
    t_atom frame_info[7];
    SETSYMBOL(&frame_info[0], x->frame[index].frame_name);
    SETSYMBOL(&frame_info[1], x->frame[index].compagny_name);
    SETFLOAT(&frame_info[2], x->frame[index].format);
    SETFLOAT(&frame_info[3], x->frame[index].point_number);
    SETFLOAT(&frame_info[4], x->frame[index].frame_number);
    SETFLOAT(&frame_info[5], x->frame[index].total_frame);
    SETFLOAT(&frame_info[6], x->frame[index].scanner);
    outlet_anything(x->x_info_outlet, gensym("frame_info"), 7, frame_info);
    
    unsigned int offset = x->frame[index].start_id;
    for ( i=0 ; i<x->frame[index].point_number ; i++ )
    {
        x->channel[CH_X].vec[i].w_float= (x->x_buf[offset+i*2] << 8) + x->x_buf[offset+i*2+1];
        x->channel[CH_Y].vec[i].w_float= (x->x_buf[offset+i*2+2] << 8) + x->x_buf[offset+i*2+3];
        
        x->channel[CH_R].vec[i].w_float= x->x_buf[offset+i*2+7] && 0x40;
        x->channel[CH_G].vec[i].w_float= x->x_buf[offset+i*2+7] && 0x40;
        x->channel[CH_B].vec[i].w_float= x->x_buf[offset+i*2+7] && 0x40;
    }
}

static void decode_format_1(t_readilda* x, unsigned int index)
{
    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

static void decode_format_2(t_readilda* x, unsigned int index)
{
    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

static void decode_format_3(t_readilda* x, unsigned int index)
{
    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

/* fin readilda.c */
