//~ udp2table.c part of ilda library
//~ receive raw data from udpreceive parse them and send them to tables

#include "m_pd.h"
#include "string.h"
#include "ilda.h"

#define PT_COORD_MAX 32767.5
#define CH_X 0
#define CH_Y 1
#define CH_R 2
#define CH_G 3
#define CH_B 4

typedef struct udp2table
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_ilda_channel channel[5];
  t_ilda_settings settings;
} t_udp2table;

t_class *udp2table_class;

void decode_settings(t_udp2table *x, unsigned int ac, t_atom* av){
    int i = 18;
    x->settings.offset_x = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.offset_y = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.scale_x =  (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.scale_y = (float) (((char) av[i].a_w.w_float << 8) + (char) av[i+1].a_w.w_float) / PT_COORD_MAX - 1.;
    i+=2;
    x->settings.invert_x = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert_y = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.blanking_off = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.invert_blancking = (char) av[i++].a_w.w_float > 0?1:0;
    x->settings.intensity = av[i++].a_w.w_float/255.;
    x->settings.mode = (char) av[i++].a_w.w_float;
    x->settings.angle_correction = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    i+=2;
    x->settings.end_line_correction = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    i+=2;
    x->settings.scan_freq = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float);
    
    t_atom output_settings[13];
    for ( i=0;i<13;i++ ){
        output_settings[i].a_type=A_FLOAT;
    }
    
    i=0;
    output_settings[i++].a_w.w_float = (float) x->settings.offset_x;
    output_settings[i++].a_w.w_float = (float) x->settings.offset_y;
    output_settings[i++].a_w.w_float = (float) x->settings.scale_x;
    output_settings[i++].a_w.w_float = (float) x->settings.scale_y;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_x;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_y;
    output_settings[i++].a_w.w_float = (float) x->settings.blanking_off;
    output_settings[i++].a_w.w_float = (float) x->settings.invert_blancking;
    output_settings[i++].a_w.w_float = (float) x->settings.intensity;
    output_settings[i++].a_w.w_float = (float) x->settings.mode;
    output_settings[i++].a_w.w_float = (float) x->settings.angle_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.end_line_correction;
    output_settings[i++].a_w.w_float = (float) x->settings.scan_freq;
    
    outlet_anything(x->m_dataout, gensym("settings"), 13, output_settings);    
}

void decode_data(t_udp2table *x, unsigned int ac, t_atom* av){
    if ( !x->channel[0].array || !x->channel[1].array || !x->channel[2].array || !x->channel[3].array || !x->channel[4].array ){
        error("you should specify x, y, r, g and b before");
        return;
    }
    
    int i=18,line_strip,j; // first number of points
    line_strip=((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float; //~ number of line strips
    
    int pt_count = 0, tmp;
    i=23; //~ first line strip
    
    for ( j=0 ; j < line_strip && i < (ac-2) ; j++ ){
        tmp=(int) ((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float;
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
    
    i = 20; // first line strip
    float r,g,b;
    j=0;
    int strip;
    for ( strip=0 ; strip < line_strip ; strip++ ){
        //~ printf("line strip %d/%d\n", strip, line_strip);
        if ( x->settings.invert_blancking ){
            r=1.-av[i].a_w.w_float / 255;
            g=1.-av[i+1].a_w.w_float / 255;
            b=1.-av[i+2].a_w.w_float / 255;
        } else {
            r=av[i].a_w.w_float / 255;
            g=av[i+1].a_w.w_float / 255;
            b=av[i+2].a_w.w_float / 255;
        }
        i+=3;
        
        pt_count = (int) ((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float;
        //~ printf("strip with %d points\n",pt_count); 
        i+=2;
        
        int k;
        for ( k = 0 ; k < pt_count && j < size && i < av-4 ; k++){  
            //~ i:index in the input frame, j:index in the table, k:index in the point table (subtable of input frame)
            float tmpx = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float)/PT_COORD_MAX - 1.;
            i+=2;
            float tmpy = (float) (((char) av[i].a_w.w_float << 8 ) + (char) av[i+1].a_w.w_float)/PT_COORD_MAX - 1.;
            i+=2;
            
            if ( x->settings.invert_x ){
                x->channel[CH_X].vec[j].w_float= (1-tmpx) * x->settings.scale_y + x->settings.offset_x;
            } else {
                x->channel[CH_X].vec[j].w_float= tmpx * x->settings.scale_y + x->settings.offset_x;
            }
            
            if ( x->settings.invert_y ){
                x->channel[CH_Y].vec[j].w_float= (1-tmpy) * x->settings.scale_x + x->settings.offset_y;
            } else {
                x->channel[CH_Y].vec[j].w_float= tmpy * x->settings.scale_x + x->settings.offset_y;
            }
            
            if ( x->settings.blanking_off ) {
                x->channel[CH_R].vec[j].w_float=1.;
                x->channel[CH_G].vec[j].w_float=1.;
                x->channel[CH_B].vec[j].w_float=1.;
            } else {
                x->channel[CH_R].vec[j].w_float=r;
                x->channel[CH_G].vec[j].w_float=g;
                x->channel[CH_B].vec[j].w_float=b;
            }
            
            if ( x->settings.invert_blancking ){
                x->channel[CH_R].vec[j].w_float=1-x->channel[CH_R].vec[j].w_float;
                x->channel[CH_G].vec[j].w_float=1-x->channel[CH_G].vec[j].w_float;
                x->channel[CH_B].vec[j].w_float=1-x->channel[CH_B].vec[j].w_float;
            }
            
            //~ printf("%d\t%.5f\t%.5f\t%.5f\n", j, x->channel[CH_X].vec[j].w_float, x->channel[CH_Y].vec[j].w_float, x->channel[CH_R].vec[j].w_float);
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

void udp2table_list(t_udp2table *x, t_symbol* s, unsigned int ac, t_atom* av){
   
    //~ validity check
    if ( ac < 17 ) {
        error("this is not an ilda frame");
        return; //~ this is not a ilda frame
    }
    
    char tag[13]="StartNewTrame";
    
    int i;
    for ( i = 0 ; i < 13 ; i++){
        if ((int) av[i].a_w.w_float != tag[i]) {
            error("wrong frame header (bad tag)");
            return;
        }
    }
    
    long unsigned int size = ((char) av[13].a_w.w_float << 24) + ((char) av[14].a_w.w_float << 16) + ((char) av[15].a_w.w_float << 8) + (char) av[16].a_w.w_float;
    
    if ( size != (ac-17) ) {
        error("wrong frame header (bad size)");
        return;
    }
    
    if ( (int) av[17].a_w.w_float ){
        decode_data(x, ac, av);
    } else {
        decode_settings(x, ac, av);
    }
    
    s=NULL;
}

static void udp2table_settab ( t_udp2table *x, t_symbol* s, unsigned int ac, t_atom* av )
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

void *udp2table_new(void)
{
    t_udp2table *x = (t_udp2table *)pd_new(udp2table_class);
    x->m_dataout = outlet_new((struct t_object *)x,0);
    
    x->settings.offset_x=0.;
    x->settings.offset_y=0.;
    x->settings.scale_x=1.;
    x->settings.scale_y=1.;
    x->settings.invert_x=0.;
    x->settings.invert_y=0.;
    x->settings.blanking_off=0.;
    x->settings.invert_blancking=0.;
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
void udp2table_setup(void)
{
    udp2table_class = class_new(gensym("udp2table"), (t_newmethod)udp2table_new, 0,
    	sizeof(t_udp2table), 0, 0);
    class_addlist(udp2table_class, udp2table_list);
    class_addmethod(udp2table_class, (t_method) udp2table_settab, gensym("settab"), A_GIMME, 0);
}


