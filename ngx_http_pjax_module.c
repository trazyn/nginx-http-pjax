
/**
 * Nginx http PJAX module
 *
 * by trazyn http://www.gituhb.com/trazyn
 * */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
	ngx_flag_t 	enable;
	ngx_str_t 	header;
	ngx_str_t 	footer;
} ngx_http_pjax_loc_conf_t;

static ngx_int_t ngx_http_pjax_init( ngx_conf_t *cf );
static void *ngx_http_pjax_create_loc_conf( ngx_conf_t *cf );
static char *ngx_http_pjax_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child );

static ngx_command_t ngx_http_pjax_commands[] = {
	{
		ngx_string( "pjax" ),
		NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof( ngx_http_pjax_loc_conf_t, enable ),
		NULL
	},
	{
		ngx_string( "pjax_header" ),
		NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof( ngx_http_pjax_loc_conf_t, header ),
		NULL
	},
	{
		ngx_string( "pjax_footer" ),
		NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof( ngx_http_pjax_loc_conf_t, footer ),
		NULL
	},

	ngx_null_command
};

static ngx_http_module_t ngx_http_pjax_module_ctx = {
	NULL, 				/** preconfiguration */
	ngx_http_pjax_init, 		/** postconfiguration */
	
	NULL, 				/** create main configuration */
	NULL, 				/** init main configuration */

	NULL, 				/** create server configuration */
	NULL, 				/** merge server configuration */

	ngx_http_pjax_create_loc_conf,	/** create location configuration */
	ngx_http_pjax_merge_loc_conf 	/** merge configuration */
};

ngx_module_t ngx_http_pjax_module = {
	NGX_MODULE_V1,
	&ngx_http_pjax_module_ctx, 	/** module context */
	ngx_http_pjax_commands, 	/** module directives */
	NGX_HTTP_MODULE, 		/** module type */

	NULL, 				/** init master */
	NULL, 				/** init module */
	NULL, 				/** init process */
	NULL, 				/** init thread */
	NULL, 				/** exit thread */
	NULL, 				/** exit process */
	NULL, 				/** exit master */
	NGX_MODULE_V1_PADDING
};

/** http://wiki.nginx.org/HeadersManagement */
ngx_table_elt_t * ngx_http_pjax_search_headers( ngx_http_request_t *r, u_char *name, size_t len ) {

	ngx_list_part_t 		*part;
	ngx_table_elt_t 		*h;
	ngx_uint_t 			i;

	/** get the first part of the list, there is usual only one part */
	part = &r->headers_in.headers.part;
	h = part->elts;

	/** header list array may consist of more than on part, so loop through all of it */
	for ( i = 0; ; ++i ) {
		if ( i >= part->nelts ) {
			if ( part->next == NULL ) {
				/** the last part, search is done */
				break;
			}

			part = part->next;
			h = part->elts;
			i = 0;
		}

		/** just compare the lengths and then the names case insensitively */
		if ( len != h[i].key.len || ngx_strcasecmp( name, h[i].key.data ) != 0 ) {
			/** this header doesn't match */
			continue;
		}

		return &h[i];
	}

	/** no headers was found */
	return NULL;
}

ngx_int_t ngx_http_pjax_add_path( ngx_http_request_t *r, ngx_array_t *filenames, ngx_str_t *path, ngx_str_t *name ) {
	
	ngx_str_t  			*filename;
	u_char 				*d;

	filename = ngx_array_push( filenames );
	if ( filename == NULL ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	filename->len = path->len + name->len;
	filename->data = ngx_pnalloc( r->pool, filename->len + 1 );
	if ( filename->data == NULL ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	d = ngx_cpymem( filename->data, path->data, path->len );
	d = ngx_cpymem( d, name->data, name->len );
	*d = '\0';

	return NGX_OK;
}

static ngx_int_t ngx_http_pjax_handler( ngx_http_request_t *r ) {

	off_t 				length;
	size_t 				last_len, root;
	time_t 				last_modified;
	u_char 				*last;
	ngx_int_t 			rc;
	ngx_buf_t 			*b;
	ngx_chain_t 			out, **last_out, *cl;
	ngx_array_t 			filenames;
	ngx_http_pjax_loc_conf_t 	*plcf;
	ngx_open_file_info_t 		of;
	ngx_http_core_loc_conf_t 	*clcf;
	ngx_str_t 			*filename, *uri, path, base;
	ngx_uint_t 			level;

	if ( !(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD)) ) {
		return NGX_HTTP_NOT_ALLOWED;
	}

	if ( r->uri.data[ r->uri.len - 1 ] == '/' ) {
		return NGX_DECLINED;
	}

	if ( rc = ngx_http_discard_request_body( r ), rc != NGX_OK ) {
		return rc;
	}

	clcf = ngx_http_get_module_loc_conf( r, ngx_http_core_module );
	plcf = ngx_http_get_module_loc_conf( r, ngx_http_pjax_module );

	if ( !plcf->enable ) {
		return NGX_DECLINED;
	}

	if ( b = ngx_pcalloc( r->pool, sizeof( ngx_buf_t ) ), b == NULL ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	last = ngx_http_map_uri_to_path( r, &path, &root, 0 );
	if ( last == NULL ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}
	path.len = last - path.data;

	base.len = root + 1;
	base.data = ngx_pnalloc( r->pool, base.len + 1 );

	ngx_cpymem( base.data, path.data, base.len );

#if (NGX_SUPPRESS_WARN)
	ngx_memzero( &filenames, sizeof( ngx_array_t ) );
#endif

	if ( ngx_array_init( &filenames, r->pool, 4, sizeof( ngx_str_t ) ) != NGX_OK ) {
		return NGX_HTTP_INTERNAL_SERVER_ERROR;
	}

	if ( ngx_http_pjax_search_headers( r, (u_char *)"X-PJAX", 6 ) ) {
		ngx_http_pjax_add_path( r, &filenames, &base, &r->uri );
	} else {
		ngx_http_pjax_add_path( r, &filenames, &base, &plcf->header );
		ngx_http_pjax_add_path( r, &filenames, &base, &r->uri );
		ngx_http_pjax_add_path( r, &filenames, &base, &plcf->footer );
	}

	last_modified = 0;
	last_len = 0;
	last_out = NULL;
	b = NULL;
	length = 0;
	uri = filenames.elts;

	for ( ngx_uint_t i = 0; i < filenames.nelts; ++i ) {

		filename = uri + i;

		if ( filename == NULL || filename->data == NULL ) {
			continue;
		}

		ngx_memzero( &of, sizeof( ngx_open_file_info_t ) );

		of.read_ahead = clcf->read_ahead;
		of.directio = clcf->directio;
		of.valid = clcf->open_file_cache_valid;
		of.min_uses = clcf->open_file_cache_min_uses;
		of.errors = clcf->open_file_cache_errors;
		of.events = clcf->open_file_cache_events;

		if ( ngx_open_cached_file( clcf->open_file_cache, filename, &of, r->pool )
				!= NGX_OK ) {

			switch ( of.err ) {
				case 0:
					return NGX_HTTP_INTERNAL_SERVER_ERROR;

				case NGX_ENOENT:
				case NGX_ENOTDIR:
				case NGX_ENAMETOOLONG:
					level = NGX_LOG_ERR;
					rc = NGX_HTTP_NOT_FOUND;
					break;

				case NGX_EACCES:
					level = NGX_LOG_ERR;
					rc = NGX_HTTP_FORBIDDEN;
					break;

				default:
					level = NGX_LOG_CRIT;
					rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
					break;
			}

			if ( rc != NGX_HTTP_NOT_FOUND || clcf->log_not_found ) {
				ngx_log_error( level, r->connection->log, of.err, 
						"%s \"%V\" failed", of.failed, filename );
			}

			return rc;
		}

		if ( !of.is_file ) {

			if ( ngx_close_file( of.fd ) == NGX_FILE_ERROR ) {
				
				ngx_log_error( NGX_LOG_ALERT, r->connection->log, ngx_errno,
						"\"%V\" is not a regular file", filename );
			}

			return NGX_DECLINED;
		}

		if ( of.size == 0 ) {
			continue;
		}

		length += of.size;

		if ( last_out == NULL ) {
			last_modified = of.mtime;
		} else {
			if ( of.mtime > last_modified ) {
				last_modified = of.mtime;
			}
		}

		b = ngx_pcalloc( r->pool, sizeof( ngx_buf_t ) );
		if ( b == NULL ) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}

		b->file = ngx_pcalloc( r->pool, sizeof( ngx_file_t ) );
		if ( b->file == NULL ) {
			return NGX_HTTP_INTERNAL_SERVER_ERROR;
		}

		b->file_pos = 0;
		b->file_last = of.size;

		b->in_file = b->file_last ? 1 : 0;

		b->file->fd = of.fd;
		b->file->name = *filename;
		b->file->log = r->connection->log;

		b->file->directio = of.is_directio;

		if ( last_out == NULL ) {
			out.buf = b;
			last_out = &out.next;
			out.next = NULL;
		} else {
			cl = ngx_alloc_chain_link( r->pool );
			if ( cl == NULL ) {
				return NGX_HTTP_INTERNAL_SERVER_ERROR;
			}

			cl->buf = b;

			*last_out = cl;
			last_out = &cl->next;
			cl->next = NULL;
		}
	}

	/** set the 'Content-Type' header */
	r->headers_out.content_type.len = sizeof( "text/html" ) - 1;
	r->headers_out.content_type.data = (u_char *)"text/html";

	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = length;
	r->headers_out.last_modified_time = last_modified;

	if ( b == NULL ) {
		r->header_only = 1;
	}

	/** send the headers of your response */
	rc = ngx_http_send_header( r );

	if ( rc == NGX_ERROR || rc > NGX_OK || r->header_only ) {
		return rc;
	}

	if ( b != NULL ) {
		b->last_in_chain = 1;
		b->last_buf = 1;
	}

	return ngx_http_output_filter( r, &out );
}


static void *ngx_http_pjax_create_loc_conf( ngx_conf_t *cf ) {

	ngx_http_pjax_loc_conf_t *conf;

	conf = ngx_pcalloc( cf->pool, sizeof( ngx_http_pjax_loc_conf_t ) );

	if ( conf == NULL ) {
		return NULL;
	}

	conf->enable = NGX_CONF_UNSET;
	conf->header.len = 0;
	conf->header.data = NULL;
	conf->footer.len = 0;
	conf->footer.data = NULL;

	return conf;
}

static char *ngx_http_pjax_merge_loc_conf( ngx_conf_t *cf, void *parent, void *child ) {

	ngx_http_pjax_loc_conf_t *prev = parent;
	ngx_http_pjax_loc_conf_t *conf = child;

	/** apply the default value */
	ngx_conf_merge_value( conf->enable, prev->enable, 0 );
	ngx_conf_merge_str_value( conf->header, prev->header, "header.html" );
	ngx_conf_merge_str_value( conf->footer, prev->footer, "footer.html" );

	return NGX_CONF_OK;
}

static ngx_int_t ngx_http_pjax_init( ngx_conf_t *cf ) {
	
	ngx_http_handler_pt 		*h;
	ngx_http_core_main_conf_t 	*cmcf;

	cmcf = ngx_http_conf_get_module_main_conf( cf, ngx_http_core_module );

	if ( h = ngx_array_push( &cmcf->phases[ NGX_HTTP_CONTENT_PHASE ].handlers ), h == NULL ) {
		return NGX_ERROR;
	}

	*h = ngx_http_pjax_handler;

	return NGX_OK;
}
