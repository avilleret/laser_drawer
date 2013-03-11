/* code for "make_ilda_frame" pd class.  This takes two messages: floating-point
numbers, and "rats", and just prints something out for each message. */

#include "m_pd.h"
#include "stdlib.h"
#ifdef HAVE_OPENCV 
#include "cv.h"
#endif

#define MAXI(a,b) (a>=b?a:b)
#define MINI(a,b) (a<=b?a:b)
#define CLIP(a,b,c) MINI(MAXI(a,b),c)
#define PT_COORD_MIN -32768
#define PT_COORD_MAX 32767
#define TRAME_SIZE_MIN 1024

    /* the data structure for each copy of "make_ilda_frame".  In this case we
    only need pd's obligatory header (of type t_object). */
typedef struct make_ilda_frame
{
  t_object x_ob;
  t_outlet *m_dataout;
  t_garray *xarray, *yarray, *colorarray;
  char *raw_data;
  short *short_data[3];
  unsigned int buffer_size, list_length;
  t_atom *trame;
#ifdef HAVE_OPENCV
  CvMat *map_matrix;
#endif
  int perspective_correction, remove_extra_zeros, zero_padding;
  
  float scale[3];
  float offset[3];

} t_make_ilda_frame;

    /* this is called back when make_ilda_frame gets a "bang" message */
void make_ilda_frame_bang(t_make_ilda_frame *x)
{
    outlet_anything(x->m_dataout, gensym("frame"), x->list_length, x->trame);
    x=NULL; /* don't warn about unused variables */
}

    /* this is called back when make_ilda_frame gets a "float" message (i.e., a
    number.) */
void make_ilda_frame_float(t_make_ilda_frame *x, t_float a)
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
	
	// truncating to remove useless OFF points
	if ( x->remove_extra_zeros ){
		for ( i=nb_point-1; i>10; i--){
			if (vec[2][i].w_float!=0) break;
			} // on scanne la table de blanking à partir de la fin et on enlève tous les 0 en laissant au moins 2 points
		nb_point=i+1;
		//~ printf("truncating done. i : \t%d/\t%d\n",i,nb_point);
	}
	
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
		x->short_data[0][i]=(int) CLIP((( *(pt) + x->offset[0] )* x->scale[0]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[1][i]=(int) CLIP((( *(pt+1) + x->offset[1] )* x->scale[1]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[2][i]=(int) CLIP((( vec[2][i].w_float + x->offset[2] )* x->scale[2]),0,255);
		//~ printf("%d\t(%d,%d,%d)\n",i,x->short_data[0][i],x->short_data[1][i],x->short_data[2][i]);
		//~ printf("%d\t(%.2f,%.2f,%.2f)\n",i,*pt,*(pt+1),x->short_data[2][i]);
		pt+=2;
	}

#else
		// copy directly from table to array
		for ( i=0;i<nb_point;i++ ){
		x->short_data[0][i]=(int) CLIP((( vec[0][i].w_float + x->offset[0] )* x->scale[0]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[1][i]=(int) CLIP((( vec[1][i].w_float + x->offset[1] )* x->scale[1]),PT_COORD_MIN,PT_COORD_MAX);
		x->short_data[2][i]=(int) CLIP((( vec[2][i].w_float + x->offset[2] )* x->scale[2]),0,255);
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
	
	if ( x->zero_padding ){
		// zero padding
		for ( i=x->list_length ; i<TRAME_SIZE_MIN ; i++){
				ap++->a_w.w_float=0.;
				x->list_length++;
		}
	}
	
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

    /* this is called when make_ilda_frame gets the message, "settab". */
void make_ilda_frame_settab(t_make_ilda_frame *x, t_symbol* s, int ac, t_atom* av)
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

void make_ilda_frame_scale(t_make_ilda_frame *x, t_symbol* s, int ac, t_atom* av){
	int i;
	if (ac != 3) {
		error("scale message need 3 float arg");
		return;
	}
	
	for (i=0;i<3;i++){
		if ( av[i].a_type == A_FLOAT )
			x->scale[i]=av[i].a_w.w_float;
		else error("scale message need 3 float arg");
	}
}

void make_ilda_frame_offset(t_make_ilda_frame *x, t_symbol* s, int ac, t_atom* av){
	int i;
	if (ac != 3) {
		error("offset message need 3 float arg");
		return;
	}
	
	for (i=0;i<3;i++){
		if ( av[i].a_type == A_FLOAT )
			x->offset[i]=av[i].a_w.w_float;
		else error("offset message need 3 float arg");
	}
}

void make_ilda_frame_dst_point(t_make_ilda_frame *x, t_symbol* s, int ac, t_atom* av)
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

void make_ilda_frame_perspective_correction(t_make_ilda_frame *x, t_symbol* s, t_float f)
{
#ifdef HAVE_OPENCV
	x->perspective_correction=(f!=0);
	return;
#else
	error("you need to compile with OpenCV to use this method (perspective_correction)");
	return;
#endif
}

void make_ilda_frame_remove_extra_zeros(t_make_ilda_frame *x, t_symbol* s, t_float f)
{
	x->remove_extra_zeros=(f!=0);
	return;
}

void make_ilda_frame_zero_padding(t_make_ilda_frame *x, t_symbol* s, t_float f)
{
	x->zero_padding=(f!=0);
	return;
}

void *make_ilda_frame_free(t_make_ilda_frame *x){
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

    /* this is a pointer to the class for "make_ilda_frame", which is created in the
    "setup" routine below and used to create new ones in the "new" routine. */
t_class *make_ilda_frame_class;


    /* this is called when a new "make_ilda_frame" object is created. */
void *make_ilda_frame_new(t_float buffer_size)
{
    t_make_ilda_frame *x = (t_make_ilda_frame *)pd_new(make_ilda_frame_class);
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

	for ( i=0;i<3;i++ ){
		x->offset[i]=0.;
		x->scale[i]=1.;
	}
	x->perspective_correction = 0;
	x->remove_extra_zeros=0;
	x->zero_padding=0;

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
void make_ilda_frame_setup(void)
{
    make_ilda_frame_class = class_new(gensym("make_ilda_frame"), (t_newmethod)make_ilda_frame_new, (t_newmethod)make_ilda_frame_free, 
			sizeof(t_make_ilda_frame), 0, A_DEFFLOAT, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_settab, gensym("settab"), A_GIMME, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_scale, gensym("scale"), A_GIMME, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_offset, gensym("offset"), A_GIMME, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_dst_point, gensym("dst_point"), A_GIMME, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_perspective_correction, gensym("perspective_correction"), A_FLOAT, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_remove_extra_zeros, gensym("remove_extra_zeros"), A_FLOAT, 0);
    class_addmethod(make_ilda_frame_class, (t_method)make_ilda_frame_zero_padding, gensym("zero_padding"), A_FLOAT, 0);
    class_addbang(make_ilda_frame_class, make_ilda_frame_bang);
    class_addfloat(make_ilda_frame_class, make_ilda_frame_float);

#ifdef HAVE_OPENCV
    post("make_ilda_frame by Antoine Villeret,\n\tbuild on %s at %s with OpenCV support.",__DATE__, __TIME__);
#else    
	post("make_ilda_frame by Antoine Villeret,\n\tbuild on %s at %s.",__DATE__, __TIME__);
#endif
}
