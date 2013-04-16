/* ildafile.c An external for Pure Data that reads and writes binary files
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
*/

/*
 * ildafile : read ilda files, part of ilda library
 * Antoine Villeret - EnsadLab 2013, antoine.villeret[AT]gmail.com
 *	based on binfile by Martin Peach 
 * 
 */ 
 
#include "m_pd.h"
#include <stdio.h>
#include <string.h>
#include "ilda.h"

#define max(a,b) (a>=b?a:b)
#define min(a,b) (a<=b?a:b)

static t_class *ildafile_class;

#define ALLOC_BLOCK_SIZE 65536 /* number of bytes to add when resizing buffer */
#define PATH_BUF_SIZE 1024 /* maximumn length of a file path */

#define MAX_FRAME_NUMBER 256 /* actually we can't store more than 256 frames, this is arbitrary */

typedef struct t_ildafile
{
    t_object    x_obj;
    t_outlet    *x_bin_outlet;
    t_outlet    *x_info_outlet;
    t_outlet    *x_bang_outlet;
    FILE        *x_fP;
    t_symbol    *x_our_directory;
    char        x_fPath[PATH_BUF_SIZE];
    unsigned char        *x_buf; /* read/write buffer in memory for file contents */
    size_t      x_buf_length; /* current length of buf */
    size_t      x_length; /* current length of valid data in buf */
    size_t      x_rd_offset; /* current read offset into the buffer */
    size_t      x_wr_offset; /* current write offset into the buffer */

    int autoresize;
    t_ilda_frame frame[MAX_FRAME_NUMBER];
    unsigned int frame_count;
    t_ilda_channel channel[6];
    t_ilda_settings settings;

} t_ildafile;

static void ildafile_free(t_ildafile *x);
static FILE *ildafile_open_path(t_ildafile *x, char *path, char *mode);
static void ildafile_read(t_ildafile *x, t_symbol *path);
static void ildafile_write(t_ildafile *x, t_symbol *path);
static void ildafile_bang(t_ildafile *x);
static void ildafile_float(t_ildafile *x, t_float val);
static void ildafile_list(t_ildafile *x, t_symbol *s, int argc, t_atom *argv);
static void ildafile_add(t_ildafile *x, t_symbol *s, int argc, t_atom *argv);
static void ildafile_clear(t_ildafile *x);
static void ildafile_settab ( t_ildafile *x, t_symbol* s, unsigned int ac, t_atom* av );
static void *ildafile_new(t_symbol *s, int argc, t_atom *argv);
static void decode_format_0(t_ildafile* x, unsigned int index);
static void decode_format_1(t_ildafile* x, unsigned int index);
static void decode_format_2(t_ildafile* x, unsigned int index);
static void decode_format_3(t_ildafile* x, unsigned int index);
void ildafile_setup(void);

void ildafile_setup(void)
{
    ildafile_class = class_new (gensym("ildafile"),
        (t_newmethod) ildafile_new,
        (t_method)ildafile_free, sizeof(t_ildafile),
        CLASS_DEFAULT,
        A_GIMME, 0);

    class_addbang(ildafile_class, ildafile_bang);
    class_addfloat(ildafile_class, ildafile_float);
    class_addlist(ildafile_class, ildafile_list);
    class_addmethod(ildafile_class, (t_method)ildafile_read, gensym("read"), A_DEFSYMBOL, 0);
    class_addmethod(ildafile_class, (t_method)ildafile_write, gensym("write"), A_DEFSYMBOL, 0);
    class_addmethod(ildafile_class, (t_method)ildafile_clear, gensym("clear"), 0);
    class_addmethod(ildafile_class, (t_method) ildafile_settab, gensym("settab"), A_GIMME, 0);
}

static void *ildafile_new(t_symbol *s, int argc, t_atom *argv)
{
    t_ildafile  *x = (t_ildafile *)pd_new(ildafile_class);
    t_symbol    *pathSymbol;
    int         i;

    if (x == NULL)
    {
        error("ildafile: Could not create...");
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
                ildafile_read(x, pathSymbol);
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
        error ("ildafile: Unable to allocate %lu bytes for buffer", x->x_buf_length);
    
    //~ x->settings.invert[2]=1;
    x->autoresize = 0;
    for (i=0;i<6;i++){
        x->channel[i].array=NULL;
        x->channel[i].arrayname=NULL;
        x->channel[i].vec=NULL;
    }

    x->x_info_outlet = outlet_new(&x->x_obj, gensym("info"));
    
    return (void *)x;
}

static void ildafile_free(t_ildafile *x)
{
    if (x->x_buf != NULL)
        freebytes(x->x_buf, x->x_buf_length);
    x->x_buf = NULL;
    x->x_buf_length = 0L;
}

static FILE *ildafile_open_path(t_ildafile *x, char *path, char *mode)
/* path is a string. Up to PATH_BUF_SIZE-1 characters will be copied into x->x_fPath. */
/* mode should be "rb" or "wb" */
/* x->x_fPath will be used as a file name to open. */
/* ildafile_open_path attempts to open the file for binary mode reading. */
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

static void ildafile_write(t_ildafile *x, t_symbol *path)
/* open the file for writing and write the entire buffer to it, then close it */
{
    size_t bytes_written = 0L;

    if (0==(x->x_fP = ildafile_open_path(x, path->s_name, "wb")))
        error("ildafile: Unable to open %s for writing", path->s_name);
    bytes_written = fwrite(x->x_buf, 1L, x->x_length, x->x_fP);
    if (bytes_written != x->x_length) post("ildafile: %ld bytes written != %ld", bytes_written, x->x_length);
    else post("ildafile: wrote %ld bytes to %s", bytes_written, path->s_name);
    fclose(x->x_fP);
    x->x_fP = NULL;
}

static void ildafile_read(t_ildafile *x, t_symbol *path)
/* open the file for reading and load it into the buffer, then close it */
{
    size_t file_length = 0L;
    size_t bytes_read = 0L;

    if (0==(x->x_fP = ildafile_open_path(x, path->s_name, "rb")))
    {
        error("ildafile: Unable to open %s for reading", path->s_name);
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
        error ("ildafile: unable to allocate %ld bytes for %s", file_length, path->s_name);
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
    if (bytes_read != file_length) post("ildafile length %ld not equal to bytes read (%ld)", file_length, bytes_read);
    else post("ildafile: read %ld bytes from %s", bytes_read, path->s_name);
    
    unsigned int i,j=0;
    x->frame_count=0;
    int count_0=0, count_1=0, count_2=0, count_3=0;
    for ( i=0 ; i<x->x_buf_length-4 && j < MAX_FRAME_NUMBER ; ){
        if ( x->x_buf[i] == 'I' && x->x_buf[i+1] == 'L' && x->x_buf[i+2] == 'D' && x->x_buf[i+3] == 'A' )
        {
            unsigned int format = (x->x_buf[i+4] << 24) + (x->x_buf[i+5] << 16) + (x->x_buf[i+6] << 8) + x->x_buf[i+7];
            x->frame[j].format=format;
            
            if ( format < 2 ){
                //~ this is a 2D or 3D frame, header is the same
                
                
                unsigned int point_number = ((x->x_buf[i+24] & 0xFF) << 8) | (x->x_buf[i+25] & 0xFF);
                if ( point_number > 0 ){
                    int k = i;
                    char name[9]={x->x_buf[k],x->x_buf[k+1],x->x_buf[k+2],x->x_buf[k+3],x->x_buf[k+4],x->x_buf[k+5],x->x_buf[k+6],x->x_buf[k+7],'\0'};
                    x->frame[j].frame_name=gensym(name);
                    k = i+8;
                    char compagny_name[9]={x->x_buf[k],x->x_buf[k+1],x->x_buf[k+2],x->x_buf[k+3],x->x_buf[k+4],x->x_buf[k+5],x->x_buf[k+6],x->x_buf[k+7],'\0'};
                    x->frame[j].compagny_name=gensym(compagny_name);
                    
                    x->frame[j].point_number=(float) point_number;
                    x->frame[j].frame_number=(float) (((x->x_buf[i+26] & 0xFF) << 8) | (x->x_buf[i+27] & 0xFF));
                    x->frame[j].total_frame=(float) (((x->x_buf[i+28] & 0xFF) << 8) | (x->x_buf[i+29] & 0xFF));
                    x->frame[j].scanner=(float) (x->x_buf[i+30] & 0xFF);
                    x->frame[j].start_id=i;
                    
                    i+=32;
                    j++;
                    x->frame_count=j;
                    if ( format == 0 ) count_0++;
                    else count_1++;
                } else { 
                    i++;
                }
            } else if ( format == 2 ){
                //~ this is a Color Lookup Table Frame
                error("Color Lookup Table Frame : i don't know how to decode it");
                count_2++;
            } else if ( format == 3 ){
                //~ this is a True Color Frame
                error("True Color Frame : idon't know how to decode it");
                count_3++;
            }
        } else {
            i++;
        }
           
    }
    
    t_atom frame_count[4];
    SETFLOAT(&frame_count[0],count_0);
    SETFLOAT(&frame_count[1],count_1);
    SETFLOAT(&frame_count[2],count_2);
    SETFLOAT(&frame_count[3],count_3);
    outlet_anything(x->x_info_outlet, gensym("file_info"), 4, frame_count);
}

static void ildafile_bang(t_ildafile *x)
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

static void ildafile_add(t_ildafile *x, t_symbol *s, int argc, t_atom *argv)
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
                error("ildafile: input (%d) out of range [0..255]", j);
                return;
            }
            if (j != f)
            {
                error("ildafile: input (%f) not an integer", f);
                return;
            }
            if (x->x_buf_length <= x->x_wr_offset)
            {
                x->x_buf = resizebytes(x->x_buf, x->x_buf_length, x->x_buf_length+ALLOC_BLOCK_SIZE);
                if (x->x_buf == NULL)
                {
                    error("ildafile: unable to resize buffer");
                    return;
                }
                x->x_buf_length += ALLOC_BLOCK_SIZE;
            }
            x->x_buf[x->x_wr_offset++] = j;
            if (x->x_length < x->x_wr_offset) x->x_length = x->x_wr_offset;
        }
        else
        {
            error("ildafile: input %d not a float", i);
            return;
        }
    }
}

static void ildafile_list(t_ildafile *x, t_symbol *s, int argc, t_atom *argv)
{
    ildafile_add(x, s, argc, argv);
}

static void ildafile_clear(t_ildafile *x)
{
    x->x_wr_offset = 0L;
    x->x_rd_offset = 0L;
    x->x_length = 0L;
    x->frame_count=0;
}

static void ildafile_float(t_ildafile *x, t_float val)
//~ retrieve given frame
{
    printf("float method\n");
    unsigned int index = (int) val<0?0:val;
    
    if ( index > x->frame_count-1 ){
        index = x->frame_count-1;
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
    int i;
    for ( i=0; i<5 ; i++){
        garray_redraw(x->channel[i].array);
    }
    
    printf("float ends ok\n");
}

static void ildafile_settab ( t_ildafile *x, t_symbol* s, unsigned int ac, t_atom* av )
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
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "i") == 0){
                index=5;
            }
            
            x->channel[index].array=tmp_array;
            x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
        } else {
            error("%s: no such array", av[2*i+1].a_w.w_symbol->s_name);           
        }
    } 
}

static void decode_format_0(t_ildafile* x, unsigned int index){
   
   printf("decode format 0\n");
   unsigned int size=x->frame[index].point_number;
   
   if ( size == 0 ) return; 
    
    printf("resize table...");
    //~ resize tables
    int i;
    for ( i=0; i<6; i++ ){
        x->channel[i].array = (t_garray *)pd_findbyclass(x->channel[i].arrayname, garray_class);
        if ( !x->channel[i].array ){
            error("can't find array %s", x->channel[i].arrayname->s_name);
            x->channel[i].vec=NULL;
        } else if (!garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){
            error("%s: bad template", x->channel[i].arrayname->s_name);
            x->channel[i].vec=NULL;
            // return;
        } else if ( x->channel[i].vecsize != size ){
            if ( x->autoresize ){
                garray_resize_long(x->channel[i].array,size);
                if (garray_getfloatwords(x->channel[i].array, &(x->channel[i].vecsize), &(x->channel[i].vec))){    
                    error("%s: can't resize correctly", x->channel[i].arrayname->s_name);
                    x->channel[i].vec=NULL;
                    // return;
                } 
            } else {
                size = min(x->channel[i].vecsize,size);
            }
        }
    }
    printf("size : %d OK\n", size);
    
    unsigned int offset = x->frame[index].start_id+32; //~ ILDA header is 32 bytes for format 0
    unsigned int j=0;
    i=offset;
    
    printf("update table, frame : %d, %d\n", index, size );
    for ( j=0 ; j< size ; j++, i+=8) //~ each point is 8 bytes in format 0
    {
        //~ bytes 0 and 1 : X coordinate
        if (x->channel[ILDA_CH_X].vec) x->channel[ILDA_CH_X].vec[j].w_float= (float) ( (short)((( x->x_buf[i] & 0xFF) << 8) | (x->x_buf[i+1] & 0xFF)) / PT_COORD_MAX );
        //~ bytes 2 and 3 : Y coordinate
        if (x->channel[ILDA_CH_Y].vec) x->channel[ILDA_CH_Y].vec[j].w_float= (float) ( (short)((( x->x_buf[i+2] & 0xFF) << 8) | (x->x_buf[i+3] & 0xFF)) / PT_COORD_MAX );
        //~ bytes 4 and 5 : Z coordinate (ignored for now...)
        //~ x->channel[ILDA_CH_Z].vec[i].w_float= (float) ( (short)((( x->x_buf[i+4] & 0xFF) << 8) | (x->x_buf[i+5] & 0xFF)) / PT_COORD_MAX );
        
        //~ byte 6 : Color Lookup Table index
        //~ byte 7 :    0x80h is the image end bit (when its 1, this is the last point)
        //~             0x40h is the blanking bit

        float intensity = (float) (1. - ((x->x_buf[i+6] >> 6) & 0x01));
        unsigned char color_id = x->x_buf[i+7];
        //~ printf("id : %d\t", i);
        //~ printf("intensity : %f\t",intensity);
        //~ printf("color_id : %d\n", color_id);

        if ( x->settings.invert[2] ){
            if (x->channel[ILDA_CH_R].vec) x->channel[ILDA_CH_R].vec[j].w_float = 1. - (float) ilda_standard_color_palette[color_id][0]/255.*intensity;
            if (x->channel[ILDA_CH_G].vec) x->channel[ILDA_CH_G].vec[j].w_float = 1. - (float) ilda_standard_color_palette[color_id][1]/255.*intensity;
            if (x->channel[ILDA_CH_B].vec) x->channel[ILDA_CH_B].vec[j].w_float = 1. - (float) ilda_standard_color_palette[color_id][2]/255.*intensity;
            if (x->channel[ILDA_CH_I].vec) x->channel[ILDA_CH_I].vec[j].w_float = 1. - intensity;
        } else {
            if (x->channel[ILDA_CH_R].vec) x->channel[ILDA_CH_R].vec[j].w_float = (float) ilda_standard_color_palette[color_id][0]/255.*intensity;
            if (x->channel[ILDA_CH_G].vec) x->channel[ILDA_CH_G].vec[j].w_float = (float) ilda_standard_color_palette[color_id][1]/255.*intensity;
            if (x->channel[ILDA_CH_B].vec) x->channel[ILDA_CH_B].vec[j].w_float = (float) ilda_standard_color_palette[color_id][2]/255.*intensity;
            if (x->channel[ILDA_CH_I].vec) x->channel[ILDA_CH_I].vec[j].w_float = intensity;
        }
    }
    printf("update table OK\n");
        
    t_atom frame_info[7];
    SETSYMBOL(&frame_info[0], x->frame[index].frame_name);
    SETSYMBOL(&frame_info[1], x->frame[index].compagny_name);
    SETFLOAT(&frame_info[2], x->frame[index].format);
    SETFLOAT(&frame_info[3], size);
    SETFLOAT(&frame_info[4], x->frame[index].frame_number);
    SETFLOAT(&frame_info[5], x->frame[index].total_frame);
    SETFLOAT(&frame_info[6], x->frame[index].scanner);
    printf("outlet\n");
    if (!x->x_info_outlet) printf("invalid outlet\n");
    outlet_anything(x->x_info_outlet, gensym("frame_info"), 7, frame_info);
    
    printf("decode format 0 OK \n");

}

static void decode_format_1(t_ildafile* x, unsigned int index)
{
       printf("decode format 1\n");

    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

static void decode_format_2(t_ildafile* x, unsigned int index)
{
       printf("decode format 2\n");

    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

static void decode_format_3(t_ildafile* x, unsigned int index)
{
       printf("decode format 3\n");

    int size=x->frame[index].point_number;
    if ( size == 0 ) return; 
}

/* fin ildafile.c */
