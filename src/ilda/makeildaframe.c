/* code for "makeildaframe" pd class.  This takes two messages: floating-point
numbers, and "rats", and just prints something out for each message. */

#include "m_pd.h"
#include "ilda.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

#define MAXI(a,b) (a>=b?a:b)
#define MINI(a,b) (a<=b?a:b)
#define CLIP(a,b,c) MINI(MAXI(a,b),c)
#define PT_COORD_MIN -32768
#define PT_COORD_MAX 32767

    /* the data structure for each copy of "makeildaframe".  In this case we
    only need pd's obligatory header (of type t_object). */
typedef struct makeildaframe
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_garray *xarray, *yarray, *colorarray;
  char *raw_data;
  short *short_data[3];
  unsigned int buffer_size, list_length;
  t_atom *trame;
  t_ilda_settings settings;
  int perspective_correction;

} t_makeildaframe;

    /* this is called back when makeildaframe gets a "bang" message */
void makeildaframe_bang(t_makeildaframe *x)
{
    outlet_anything(x->m_dataout, gensym("frame"), x->list_length, x->trame);
    x=NULL; /* don't warn about unused variables */
}

    /* this is called back when makeildaframe gets a "float" message (i.e., a
    number.) */
void makeildaframe_float(t_makeildaframe *x, t_float a)
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

void makeildaframe_send_setting(t_makeildaframe *x)
{
    t_atom frame[(ILDA_SETTING_SIZE + ILDA_HEADER_SIZE)*sizeof(t_atom)];
    
    makeildaframe_makeheader(&frame, ILDA_SETTING_SIZE, 1);
    
    char setting_trame[ILDA_SETTING_SIZE];
    
    union intfloat32
    {
        int     i;
        float   f;
    };
    union intfloat32 if32;
    
    int i = 0;
    if32.f = x->settings.offset[0];
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.offset[1];
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.scale[0];
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.scale[1];
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.intensity;
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.angle_correction;
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.end_line_correction;
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    if32.f = x->settings.scan_freq;
    *((int4byte *) setting_trame+i) = htonl(if32.i);
    i+=4;
    
    int j;
    for ( j=0; j < i ; j++){
        SETFLOAT(&frame[i], setting_trame[i]);
    }
    
    SETFLOAT(&frame[i], x->settings.invert[0]);
    i++;
    SETFLOAT(&frame[i], x->settings.invert[1]);
    i++;
    SETFLOAT(&frame[i], x->settings.invert[3]);
    i++;
    SETFLOAT(&frame[i], x->settings.blanking_off);
    i++;
    
    SETFLOAT(&frame[i], x->settings.mode);
    i++;
    
    
    outlet_list(x->m_dataout, gensym("ildaframe"), i, frame);
}

void makeildaframe_makeheader(t_atom *frame, int size, int type)
{
    SETFLOAT(&frame[0],'I');
    SETFLOAT(&frame[1],'L');
    SETFLOAT(&frame[2],'D');
    SETFLOAT(&frame[3],'A');
    
    SETFLOAT(&frame[4], (size & 0xFF000000) >> 24);
    SETFLOAT(&frame[5], (size & 0xFF0000) >> 16);
    SETFLOAT(&frame[6], (size & 0xFF00) >> 8);
    SETFLOAT(&frame[7], size & 0xFF);
    
    SETFLOAT(&frame[8], type); //~ 0 is data trame, 1 is setting trame
}

    /* this is called when makeildaframe gets the message, "settab". */
void makeildaframe_settab(t_makeildaframe *x, t_symbol* s, int ac, t_atom* av)
{
	if (ac != 3 ) {
		error("settab need 3 args : X,Y and color");
		return;
	}
	if ( av[0].a_type != A_SYMBOL || av[1].a_type != A_SYMBOL || av[2].a_type != A_SYMBOL ) {
		error("settab args must be symbol");
		return;
	}
	
	// check if arrays exist
	if (!(x->xarray = (t_garray *)pd_findbyclass(av[0].a_w.w_symbol, garray_class)))
		error("%s: no such array", av[0].a_w.w_symbol->s_name);
	if (!(x->yarray = (t_garray *)pd_findbyclass(av[1].a_w.w_symbol, garray_class)))
		error("%s: no such array", av[1].a_w.w_symbol->s_name);
	if (!(x->colorarray = (t_garray *)pd_findbyclass(av[2].a_w.w_symbol, garray_class)))
		error("%s: no such array", av[2].a_w.w_symbol->s_name);
		
    x=NULL; /* don't warn about unused variables */
}

void makeildaframe_scale(t_makeildaframe *x, t_symbol* s, int ac, t_atom* av){
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
}

void makeildaframe_offset(t_makeildaframe *x, t_symbol* s, int ac, t_atom* av){
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
    
}

void makeildaframe_invert(t_makeildaframe *x, t_symbol* s, int ac, t_atom* av)
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
}

void makeildaframe_dst_point(t_makeildaframe *x, t_symbol* s, int ac, t_atom* av)
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

void makeildaframe_perspective_correction(t_makeildaframe *x, t_symbol* s, t_float f)
{
#ifdef HAVE_OPENCV
	x->perspective_correction=(f!=0);
	return;
#else
	error("you need to compile with OpenCV to use this method (perspective_correction)");
	return;
#endif
}

void *makeildaframe_free(t_makeildaframe *x){
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

    /* this is a pointer to the class for "makeildaframe", which is created in the
    "setup" routine below and used to create new ones in the "new" routine. */
t_class *makeildaframe_class;


    /* this is called when a new "makeildaframe" object is created. */
void *makeildaframe_new(t_float buffer_size)
{
    t_makeildaframe *x = (t_makeildaframe *)pd_new(makeildaframe_class);
	if ( !buffer_size ) {
		error("first arg (buffer size) should be > 0");
		return;
	}
    x->m_dataout = outlet_new(x,0);
    x->xarray=NULL;
    x->yarray=NULL;
    x->colorarray=NULL;
    x->raw_data=NULL;
    x->short_data[0]=NULL;
    x->short_data[1]=NULL;
    x->short_data[2]=NULL;
    x->buffer_size=buffer_size;
    x->raw_data= (char *) malloc(sizeof(char)*x->buffer_size);
    x->short_data[0]= ( short *) malloc(sizeof(short)*x->buffer_size);
    x->short_data[1]= ( short *) malloc(sizeof(short)*x->buffer_size);
    x->short_data[2]= ( short *) malloc(sizeof(short)*x->buffer_size);
	x->trame = (t_atom *) malloc(sizeof(t_atom)*(x->buffer_size*9+3)); // au maximum en faisant des segments de 1 point, on a 9 bytes par segment + 3 bytes d'entête
	
	if (!x->raw_data || !x->short_data[0] || !x->short_data[1] || !x->short_data[2] || !x->trame){
		error("could not allocate memory");
		return;
	}
	unsigned int i;
	for ( i=0;i<(x->buffer_size*9+3);i++ ){
		x->trame[i].a_type=A_FLOAT; // trame is only populate with float
		x->trame[i].a_w.w_float = 0;
	}
    
    x->settings.offset[0]=0.;
    x->settings.offset[1]=0.;
    x->settings.scale[0]=1.;
    x->settings.scale[1]=1.;

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
void makeildaframe_setup(void)
{
    makeildaframe_class = class_new(gensym("makeildaframe"), (t_newmethod)makeildaframe_new, (t_newmethod)makeildaframe_free, 
			sizeof(t_makeildaframe), 0, A_DEFFLOAT, 0);
    class_addmethod(makeildaframe_class, (t_method)makeildaframe_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(makeildaframe_class, (t_method)makeildaframe_dst_point, gensym("dst_point"), A_GIMME, 0);
    class_addmethod(makeildaframe_class, (t_method)makeildaframe_perspective_correction, gensym("perspective_correction"), A_FLOAT, 0);
    class_addmethod(makeildaframe_class, (t_method)makeildaframe_offset, gensym("offset"), A_GIMME, 0);
    class_addmethod(makeildaframe_class, (t_method)makeildaframe_scale, gensym("scale"), A_GIMME, 0);
    class_addbang(makeildaframe_class, makeildaframe_bang);
    class_addfloat(makeildaframe_class, makeildaframe_float);

#ifdef HAVE_OPENCV
    post("makeildaframe by Antoine Villeret,\n\tbuild on %s at %s with OpenCV support.",__DATE__, __TIME__);
#else    
	post("makeildaframe by Antoine Villeret,\n\tbuild on %s at %s.",__DATE__, __TIME__);
#endif
}
