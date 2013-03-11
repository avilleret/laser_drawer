/* code for "ildasend" pd class.  This takes two messages: floating-point
numbers, and "rats", and just prints something out for each message. */

#include "m_pd.h"
#include "ilda.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

#define MAXI(a,b) (a>=b?a:b)
#define MINI(a,b) (a<=b?a:b)
#define CLIP(a,b,c) MINI(MAXI(a,b),c)
#define PT_COORD_MIN -320

    /* the data structure for each copy of "ildasend".  In this case we
    only need pd's obligatory header (of type t_object). */
typedef struct ildasend
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_garray *xarray, *yarray, *colorarray;
  char *raw_data;
  short *short_data[3];
  t_ilda_channel channel[5];
  unsigned int buffer_size, list_length;
  t_atom *trame;
  t_ilda_settings settings;
  int perspective_correction;
  lo_address OSC_destination;

} t_ildasend;

    /* this is called back when ildasend gets a "bang" message */
void ildasend_bang(t_ildasend *x)
{
    if( !x->OSC_destination ){
        error("not connected");
        return;
    }
    
    int i; 
    for (i=0;i<5;i++){
        if (x->channel[i].array){
            if (!garray_getfloatwords(x->channel[i].array, &x->channel[i].vecsize, &x->channel[i].vec)){
                error("error when getting data from array...");
            } else {
                lo_blob blob = lo_blob_new(x->channel[i].vecsize*sizeof(t_word),x->channel[i].vec);
                char path[30];
                sprintf(path, "/array/%s", x->channel[i].channelname->s_name);
                lo_send(x->OSC_destination, path, "b", blob);
            }
        }
    }
}

    /* this is called back when ildasend gets a "float" message (i.e., a
    number.) */

void ildasend_float(t_ildasend *x, t_float a)
{
    t_word *vec[3];
    unsigned int nb_point, tab_size, i;
    int size[3];
    x->list_length=0;
    
	if( !x->xarray || !x->yarray || !x->colorarray ){
		error("settab first");
		return;
	}
	
	// copy data from table to vec array
    if (!garray_getfloatwords(x->xarray, &(size[0]), &(vec[0]))) error("error when getting data from array...");
    if (!garray_getfloatwords(x->yarray, &(size[1]), &(vec[1]))) error("error when getting data from array...");
    if (!garray_getfloatwords(x->colorarray, &(size[2]), &(vec[2]))) error("error when getting data from array...");

    tab_size=MINI(MINI(size[0],size[1]),size[2]);
    if (tab_size>x->buffer_size){
		error("buffer overflow, data truncated...");
		tab_size=x->buffer_size;
	}
    a=MAXI(10,a);
	nb_point=MINI(a,tab_size);
	
#ifdef HAVE_OPENCV 
	// using OpenCV for perspective correction
	CvMat *src, *dst;
	src=cvCreateMat(nb_point, 1, CV_32FC2 );
	
	uchar *pt;
	pt=src->data.ptr;
	
	// copying data to CvMat needed because of t_word structure which is unusal
	for ( i=0 ; i<nb_point ; i++ ) {
		*(pt++)=vec[0][i].w_float; // Replacement for CV_MAT_ELEM broken in 2.3.1
		*(pt++)=vec[1][i].w_float; // Replacement for CV_MAT_ELEM broken in 2.3.1
	}
		
	if ( x->perspective_correction ){
		dst=cvCreateMat(nb_point, 1, CV_32FC2 );
		cvPerspectiveTransform(src, dst, x->map_matrix);
	} else dst=src; // if no transformation, just point to src data
	
	pt=dst->data.ptr;
	
	// converting float to short
	for ( i=0;i<nb_point;i++ ){
		x->short_data[0][i]=(int) CLIP((( *(pt) + x->settings.offset[0] )* x->settings.scale[0]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[1][i]=(int) CLIP((( *(pt+1) + x->settings.offset[1] )* x->settings.scale[1]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[2][i]=(int) CLIP((( vec[2][i].w_float + x->offset[2] )* x->scale[2]),0,255);
		//~ printf("%d\t(%d,%d,%d)\n",i,x->short_data[0][i],x->short_data[1][i],x->short_data[2][i]);
		//~ printf("%d\t(%.2f,%.2f,%.2f)\n",i,*pt,*(pt+1),x->short_data[2][i]);
		pt+=2;
	}

#else
		// copy directly from table to array
		for ( i=0;i<nb_point;i++ ){
		x->short_data[0][i]=(int) CLIP((( vec[0][i].w_float + x->settings.offset[0] )* x->settings.scale[0]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[1][i]=(int) CLIP((( vec[1][i].w_float + x->settings.offset[1] )* x->settings.scale[1]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[2][i]=(int) CLIP((  vec[2][i].w_float * 255),0,255);
	}
#endif
	
	t_atom *ap, *bp;
	ap = &x->trame[3]; // position dans la trame

	unsigned int nb_segment=0;

	for ( i=0 ; i<nb_point ; ){
		
		short cur_color = CLIP(x->short_data[2][i],0,255);
		int seg_pt_nb=0; // compteur du nombre de points du segment
		
		ap++->a_w.w_float = (float) cur_color; // red
		ap++->a_w.w_float = (float) cur_color; // green
		ap++->a_w.w_float = (float) cur_color; // blue
		x->list_length+=3;
		cur_color = x->short_data[2][i]; // non-clipped value for comparaison above
		bp=ap+2;
		
		for ( ; ; ){ // infinite loop ?
			bp++->a_w.w_float= (float) ((x->short_data[0][i] & 0xff00) >> 8); // MSB du x du point
			bp++->a_w.w_float= (float) (x->short_data[0][i] & 0xff); // LSB du x du point
			bp++->a_w.w_float= (float) ((x->short_data[1][i] & 0xff00) >> 8); // MSB du y du point
			bp++->a_w.w_float= (float) (x->short_data[1][i] & 0xff); // LSB du y du point
			i++;
			seg_pt_nb++;
			x->list_length+=4;
			
			if ( i>=nb_point ) { /* printf("i>=nb_point (%d >= %d): break !\n", i, nb_point); */ break;}
			if (cur_color!=x->short_data[2][i]) { /* printf("cur_color changed\n"); */ break;} // break the loop !
		}
		ap++->a_w.w_float= (float) ((seg_pt_nb & 0xff00)>>8); // MSB du nb de point du segment
		ap++->a_w.w_float= (float) (seg_pt_nb & 0xff); // LSB du nb de point du segment
		x->list_length+=2;
		ap=bp;
		nb_segment++;
	}
	x->trame[0].a_w.w_float = 1.; // trame status
	x->trame[1].a_w.w_float = (float) ((nb_segment & 0xff00) >> 8); // MSB du nb de segment
	x->trame[2].a_w.w_float = (float) (nb_segment & 0xff); // LSB du nb de segment
	x->list_length+=3;
	
	//~ printf("send %d bytes\n", x->list_length);
	outlet_anything(x->m_dataout, gensym("frame"), x->list_length, x->trame);

#ifdef HAVE_OPENCV
	//~ Release Matrix data
	if (dst!=src) { 
		cvReleaseMat(&dst);
		dst=NULL;
	}
	if (src) {
		cvReleaseMat(&src);
		src=NULL;
	}
#endif
	
	ap=NULL;
	
    x=NULL; /* don't warn about unused variables */
}

void ildasend_send_setting(t_ildasend *x)
{
    lo_send(x->OSC_destination, "/setting/offset", "fff", x->settings.offset[0], x->settings.offset[1], x->settings.offset[2] );
    lo_send(x->OSC_destination, "/setting/scale", "fff", x->settings.scale[0], x->settings.scale[1], x->settings.scale[2] );
    lo_send(x->OSC_destination, "/setting/invert", "fff", x->settings.invert[0], x->settings.invert[1], x->settings.invert[2] );
    lo_send(x->OSC_destination, "/setting/intensity", "fff", x->settings.intensity[0], x->settings.intensity[1], x->settings.intensity[2] );
    lo_send(x->OSC_destination, "/setting/blanking_off", "f", x->settings.blanking_off );
    lo_send(x->OSC_destination, "/setting/angle_correction", "f", x->settings.angle_correction );
    lo_send(x->OSC_destination, "/setting/end_line_correction", "f", x->settings.end_line_correction );
    lo_send(x->OSC_destination, "/setting/scan_freq", "f", x->settings.scan_freq );
}

    /* this is called when ildasend gets the message, "settab". */
static void ildasend_settab ( t_ildasend *x, t_symbol* s, unsigned int ac, t_atom* av )
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
                index=ILDA_CH_X;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "y") == 0){
                index=ILDA_CH_Y;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "r") == 0){
                index=ILDA_CH_R;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "g") == 0){
                index=ILDA_CH_G;
            } else if ( strcmp( av[2*i].a_w.w_symbol->s_name, "b") == 0){
                index=ILDA_CH_B;
            }
            
            x->channel[index].array=tmp_array;
            x->channel[index].arrayname=av[2*i+1].a_w.w_symbol;
        } else {
            error("%s: no such array", av[2*i+1].a_w.w_symbol->s_name);           
        }
    } 
}

void ildasend_connect(t_ildasend *x, t_symbol* hostname, float port){
    if ( x->OSC_destination ) {
        error("already connected");
        return;
    }
    
    char portstr[30];
    sprintf(portstr, "%d", (int) port);
    x->OSC_destination = lo_address_new(hostname->s_name,portstr);
    
    if ( x->OSC_destination ){
        outlet_float(x->m_dataout, 1.);
    } else {
        outlet_float(x->m_dataout, 0.);
    }
}

void ildasend_scale(t_ildasend *x, t_symbol* s, int ac, t_atom* av){
	int i;
	if (ac < 2) {
		error("scale message need at least 2 float arg");
		return;
	}
	
	for (i=0;i<ac;i++){
		if ( av[i].a_type == A_FLOAT )
			x->settings.scale[i]=av[i].a_w.w_float;
		else error("scale message accepts only float arg");
	}
    
    lo_send(x->OSC_destination, "/setting/scale", "fff", x->settings.scale[0], x->settings.scale[1], x->settings.scale[2] );

}

void ildasend_offset(t_ildasend *x, t_symbol* s, int ac, t_atom* av){
	int i;
	if (ac < 2) {
		error("offset message need at least 2 float arg");
		return;
	}
	
	for (i=0;i<ac;i++){
		if ( av[i].a_type == A_FLOAT )
			x->settings.offset[i]=av[i].a_w.w_float;
		else error("offset message accepts only float arg");
	}
    
    lo_send(x->OSC_destination, "/setting/offset", "fff", x->settings.offset[0], x->settings.offset[1], x->settings.offset[2] );
}

void ildasend_invert(t_ildasend *x, t_symbol* s, int ac, t_atom* av)
{
    int i;
    if (ac < 2) {
		error("invert message need at least 2 float arg");
		return;
	}
    
    for (i=0;i<ac;i++){
        if ( av[i].a_type == A_FLOAT )
			x->settings.invert[i]=av[i].a_w.w_float>0?1:0;
		else error("invert message accept only float arg");
    }
    
    lo_send(x->OSC_destination, "/setting/invert", "fff", x->settings.invert[0], x->settings.invert[1], x->settings.invert[2] );

}

void ildasend_dst_point(t_ildasend *x, t_symbol* s, int ac, t_atom* av)
{
#ifdef HAVE_OPENCV

	int i;
	if (ac != 8) {
		error("wrong arg number : dst_point message need 8 float arg (4 x,y pairs)");
		return;
	}
	
	for (i=0;i<8;i++){
		if (av[i].a_type != A_FLOAT) {
			error("wrong arg type : dst_point message need 8 float arg (4 x,y pairs)");
			return;
		}
	}
	
	CvPoint2D32f src_point[4], dst_point[4];
	
	src_point[0].x=-1;
	src_point[0].y=-1;
	src_point[1].x=1;
	src_point[1].y=-1;
	src_point[2].x=1;
	src_point[2].y=1;
	src_point[3].x=-1;
	src_point[3].y=1;
	
	for ( i=0; i<4; i++){
		dst_point[i].x=av[i*2].a_w.w_float;
		dst_point[i].y=av[i*2+1].a_w.w_float;
	}
	
	cvGetPerspectiveTransform( src_point, dst_point , x->map_matrix );

	//~ printf("mapMatrix :\n");
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,0), CV_MAT_ELEM(*(x->map_matrix),float,1,0), CV_MAT_ELEM(*(x->map_matrix),float,2,0));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,1), CV_MAT_ELEM(*(x->map_matrix),float,1,1), CV_MAT_ELEM(*(x->map_matrix),float,2,1));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,2), CV_MAT_ELEM(*(x->map_matrix),float,1,2), CV_MAT_ELEM(*(x->map_matrix),float,2,2));
	return;
#else 
	error("you need to compile with OpenCV to use this method (%s)",s->s_name);
	return;
#endif
}

void ildasend_perspective_correction(t_ildasend *x, t_symbol* s, t_float f)
{
#ifdef HAVE_OPENCV
	x->perspective_correction=(f!=0);
	return;
#else
	error("you need to compile with OpenCV to use this method (perspective_correction)");
	return;
#endif
}

void *ildasend_free(t_ildasend *x){
	free(x->raw_data);
	free(x->short_data[0]);
	free(x->short_data[1]);
	free(x->short_data[2]);
	free(x->trame);
	x->raw_data=NULL;
	x->short_data[0]=NULL;
	x->short_data[1]=NULL;
	x->short_data[2]=NULL;
	x->trame=NULL;
#ifdef HAVE_OPENCV
	cvReleaseMat(&x->map_matrix);
#endif
}

    /* this is a pointer to the class for "ildasend", which is created in the
    "setup" routine below and used to create new ones in the "new" routine. */
t_class *ildasend_class;


    /* this is called when a new "ildasend" object is created. */
void *ildasend_new()
{
    t_ildasend *x = (t_ildasend *)pd_new(ildasend_class);
    x->m_dataout = outlet_new(x,0);
    x->OSC_destination = NULL;
    
    unsigned int i;
    for ( i=0;i<5;i++ ){
        x->channel[i].array=NULL;
        x->channel[i].arrayname=NULL;
    }
        
    x->settings.offset[0]=0.;
    x->settings.offset[1]=0.;
    x->settings.scale[0]=1.;
    x->settings.scale[1]=1.;
    
    x->channel[ILDA_CH_X].channelname=gensym("x");
    x->channel[ILDA_CH_Y].channelname=gensym("y");
    x->channel[ILDA_CH_R].channelname=gensym("r");
    x->channel[ILDA_CH_G].channelname=gensym("g");
    x->channel[ILDA_CH_B].channelname=gensym("b");

	x->perspective_correction = 0;

#ifdef HAVE_OPENCV
	x->map_matrix=cvCreateMat(3,3,CV_32FC1);
	cvSetIdentity(x->map_matrix,cvRealScalar(1));
#endif

	//~ printf("mapMatrix :\n");
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,0), CV_MAT_ELEM(*(x->map_matrix),float,1,0), CV_MAT_ELEM(*(x->map_matrix),float,2,0));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,1), CV_MAT_ELEM(*(x->map_matrix),float,1,1), CV_MAT_ELEM(*(x->map_matrix),float,2,1));
	//~ printf("%.2f\t%.2f\t%.2f\n", CV_MAT_ELEM(*(x->map_matrix),float,0,2), CV_MAT_ELEM(*(x->map_matrix),float,1,2), CV_MAT_ELEM(*(x->map_matrix),float,2,2));
	//~ 
    return (void *)x;
}

    /* this is called once at setup time, when this code is loaded into Pd. */
void ildasend_setup(void)
{
    ildasend_class = class_new(gensym("ildasend"), (t_newmethod)ildasend_new, (t_newmethod)ildasend_free, 
			sizeof(t_ildasend), 0, A_DEFFLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_dst_point, gensym("dst_point"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_perspective_correction, gensym("perspective_correction"), A_FLOAT, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_offset, gensym("offset"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_scale, gensym("scale"), A_GIMME, 0);
    class_addmethod(ildasend_class, (t_method)ildasend_connect, gensym("connect"), A_SYMBOL, A_FLOAT, 0);
    class_addbang(ildasend_class, ildasend_bang);
    class_addfloat(ildasend_class, ildasend_float);

#ifdef HAVE_OPENCV
    post("ildasend by Antoine Villeret,\n\tbuild on %s at %s with OpenCV support.",__DATE__, __TIME__);
#else    
	post("ildasend by Antoine Villeret,\n\tbuild on %s at %s.",__DATE__, __TIME__);
#endif
}
