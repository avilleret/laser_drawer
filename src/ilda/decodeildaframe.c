//~ decodeildaframe.c part of ilda library
//~ receive raw data from udpreceive parse them and send them to tables

#include "m_pd.h"
#include "string.h"
#include "ilda.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

typedef struct decodeildaframe
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_ilda_channel channel[5];
  t_ilda_settings settings;
#ifdef HAVE_OPENCV
  CvMat *map_matrix;
#endif
} t_decodeildaframe;

t_class *decodeildaframe_class;

void decode_settings(t_decodeildaframe *x, unsigned int ac, t_atom* av){
    int i = ILDA_HEADER_SIZE;
    if ( ac != ILDA_HEADER_SIZE + ILDA_SETTING_SIZE ){
        error("wrong settings size");
        return;
    }
    
    x->settings.offset[0] = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    x->settings.offset[1] = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    x->settings.scale[0] =  (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    x->settings.scale[1] =  (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    
    
    x->settings.invert[0] = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert[1] = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert[2] = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.blanking_off = (char) av[i++].a_w.w_float > 0?1:0;
    
    x->settings.intensity = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    
    x->settings.mode = (char) av[i++].a_w.w_float;
    
    x->settings.angle_correction = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    x->settings.end_line_correction = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    i+=4;
    x->settings.scan_freq = (float) ((((char) av[i].a_w.w_float & 0xFF) << 24) | (((char) av[i+1].a_w.w_float & 0xFF) << 16) | (((char) av[i+2].a_w.w_float & 0xFF) << 8) | ((char) av[i+3].a_w.w_float & 0xFF));
    
    t_atom output_settings[13];
    for ( i=0;i<13;i++ ){
        output_settings[i].a_type=A_FLOAT;
    }
    
    i=0;
    output_settings[i++].a_w.w_float = (float) x->settings.offset[0];
    output_settings[i++].a_w.w_float = (float) x->settings.offset[1];
    output_settings[i++].a_w.w_float = (float) x->settings.scale[0];
    output_settings[i++].a_w.w_float = (float) x->settings.scale[1];
    output_settings[i++].a_w.w_float = (float) x->settings.invert[0];
    output_settings[i++].a_w.w_float = (float) x->settings.invert[1];
    output_settings[i++].a_w.w_float = (float) x->settings.blanking_off;
    output_settings[i++].a_w.w_float = (float) x->settings.invert[2];
    output_settings[i++].a_w.w_float = (float) x->settings.intensity;
    output_settings[i++].a_w.w_float = (float) x->settings.mode;
    output_settings[i++].a_w.w_float = (float) x->settings.angle_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.end_line_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.scan_freq;
    
    outlet_anything(x->m_dataout, gensym("settings"), 13, output_settings);    
}

void decode_data(t_decodeildaframe *x, unsigned int ac, t_atom* av){
    if ( !x->channel[0].array || !x->channel[1].array || !x->channel[2].array || !x->channel[3].array || !x->channel[4].array ){
        error("you should specify x, y, r, g and b before");
        return;
    }
    
    int i=ILDA_HEADER_SIZE,line_strip,j; // first number of points
    line_strip=(((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF); //~ number of line strips
    
    int pt_count = 0, tmp;
    i+=5; //~ first line strip
    
    for ( j=0 ; j < line_strip && i < (ac-2) ; j++ ){
        tmp=(int) (((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF);
        //~ printf("strip %d/%d with %d point\n", j, line_strip, tmp);
        pt_count+=tmp;
        i+=tmp*4+5;
    }
    
    int size=pt_count;
    //~ printf("total number of point : %d\n", size);
    
    //~ resize tables
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
    
    i = ILDA_HEADER_SIZE + 2; // first line strip
    float r,g,b;
    j=0;
    int strip;
    for ( strip=0 ; strip < line_strip ; strip++ ){
        //~ printf("line strip %d/%d\n", strip, line_strip);
        if ( x->settings.invert[2] ){
            r=1.-av[i].a_w.w_float / 255;
            g=1.-av[i+1].a_w.w_float / 255;
            b=1.-av[i+2].a_w.w_float / 255;
        } else {
            r=av[i].a_w.w_float / 255;
            g=av[i+1].a_w.w_float / 255;
            b=av[i+2].a_w.w_float / 255;
        }
        i+=3;
        
        pt_count = (int) (((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF);
        //~ printf("strip with %d points\n",pt_count); 
        i+=2;
        
        int k;
        for ( k = 0 ; k < pt_count && j < size && i < av-4 ; k++){  
            //~ i:index in the input frame, j:index in the table, k:index in the point table (subtable of input frame)
            float tmpx = (float) ((((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF))/PT_COORD_MAX - 1.;
            i+=2;
            float tmpy = (float) ((((char) av[i].a_w.w_float & 0xFF) << 8 ) | ((char) av[i+1].a_w.w_float & 0xFF))/PT_COORD_MAX - 1.;
            i+=2;
            
            if ( x->settings.invert[0] ){
                x->channel[ILDA_CH_X].vec[j].w_float= (1-tmpx) * x->settings.scale[1] + x->settings.offset[0];
            } else {
                x->channel[ILDA_CH_X].vec[j].w_float= tmpx * x->settings.scale[1] + x->settings.offset[0];
            }
            
            if ( x->settings.invert[1] ){
                x->channel[ILDA_CH_Y].vec[j].w_float= (1-tmpy) * x->settings.scale[0] + x->settings.offset[1];
            } else {
                x->channel[ILDA_CH_Y].vec[j].w_float= tmpy * x->settings.scale[0] + x->settings.offset[1];
            }
            
            if ( x->settings.blanking_off ) {
                x->channel[ILDA_CH_R].vec[j].w_float=1.;
                x->channel[ILDA_CH_G].vec[j].w_float=1.;
                x->channel[ILDA_CH_B].vec[j].w_float=1.;
            } else {
                x->channel[ILDA_CH_R].vec[j].w_float=r;
                x->channel[ILDA_CH_G].vec[j].w_float=g;
                x->channel[ILDA_CH_B].vec[j].w_float=b;
            }
            
            if ( x->settings.invert[2] ){
                x->channel[ILDA_CH_R].vec[j].w_float=1-x->channel[ILDA_CH_R].vec[j].w_float;
                x->channel[ILDA_CH_G].vec[j].w_float=1-x->channel[ILDA_CH_G].vec[j].w_float;
                x->channel[ILDA_CH_B].vec[j].w_float=1-x->channel[ILDA_CH_B].vec[j].w_float;
            }
            
            //~ printf("%d\t%.5f\t%.5f\t%.5f\n", j, x->channel[ILDA_CH_X].vec[j].w_float, x->channel[ILDA_CH_Y].vec[j].w_float, x->channel[ILDA_CH_R].vec[j].w_float);
            j++;
        }
    }
    for ( i=0;i<5;i++) {
        garray_redraw(x->channel[i].array);
    }

    
    x=NULL;
    ac=0;
    av=NULL;
}

void decodeildaframe_list(t_decodeildaframe *x, t_symbol* s, unsigned int ac, t_atom* av){
   
    //~ validity check
    if ( ac < 17 ) {
        error("this is not an ilda frame");
        return; //~ this is not a ilda frame
    }
    
    char tag[4]="ILDA";
    
    int i;
    for ( i = 0 ; i < 4 ; i++){
        if ((int) av[i].a_w.w_float != tag[i]) {
            error("wrong frame header (bad tag)");
            return;
        }
    }
    i = 4;
    unsigned int size = (( (char) av[i].a_w.w_float & 0xFF) << 24) | (( (char) av[i+1].a_w.w_float & 0xFF) << 16) |(( (char) av[i+2].a_w.w_float & 0xFF) << 8) |( (char) av[i+3].a_w.w_float & 0xFF);
    
    if ( size != (ac-ILDA_HEADER_SIZE) ) {
        error("wrong frame header (bad size)");
        return;
    }
    
    if ( (int) av[8].a_w.w_float == 0. ){
        decode_data(x, ac, av);
    } else {
        decode_settings(x, ac, av);
    }
    
    s=NULL;
}

static void decodeildaframe_settab ( t_decodeildaframe *x, t_symbol* s, unsigned int ac, t_atom* av )
{
    if (ac%2 != 0 ) {
		error("wong arg number, usage : settab <x|y|r|g|b> <table name>");
		return;
	}
    int i=0;
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

void *decodeildaframe_new(void)
{
    t_decodeildaframe *x = (t_decodeildaframe *)pd_new(decodeildaframe_class);
    x->m_dataout = outlet_new((struct t_object *)x,0);
    
    x->settings.offset[0]=0.;
    x->settings.offset[1]=0.;
    x->settings.scale[0]=1.;
    x->settings.scale[1]=1.;
    x->settings.invert[0]=0.;
    x->settings.invert[1]=0.;
    x->settings.invert[2]=0.;
    x->settings.blanking_off=0.;
    x->settings.intensity=1.;
    x->settings.mode=0;
    x->settings.angle_correction=0.;
    x->settings.end_line_correction=0.;
    x->settings.scan_freq=10;
    
    x->channel[0].channelname=gensym("x");
    x->channel[1].channelname=gensym("y");
    x->channel[2].channelname=gensym("r");
    x->channel[3].channelname=gensym("g");
    x->channel[4].channelname=gensym("b");
    
    int i;
    for ( i=0;i<5;i++ ){
        x->channel[i].array=NULL;
        x->channel[i].arrayname=NULL;
    }
    
    return (void *)x;
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void decodeildaframe_setup(void)
{
    decodeildaframe_class = class_new(gensym("decodeildaframe"), (t_newmethod)decodeildaframe_new, 0,
    	sizeof(t_decodeildaframe), 0, 0);
    class_addlist(decodeildaframe_class, decodeildaframe_list);
    class_addmethod(decodeildaframe_class, (t_method) decodeildaframe_settab, gensym("settab"), A_GIMME, 0);
}


