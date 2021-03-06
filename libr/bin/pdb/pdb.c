/* radare - LGPL - Copyright 2014 - inisider */

#include <r_pdb.h>
#include <string.h>

#include "stream_file.h"
#include "types.h"
#include "tpi.h"
#include "dbi.h"
#include "fpo.h"
#include "gdata.h"
#include "pe.h"
#include "omap.h"

#define PDB2_SIGNATURE "Microsoft C/C++ program database 2.00\r\n\032JG\0\0"
#define PDB2_SIGNATURE_LEN 51

#define PDB7_SIGNATURE "Microsoft C/C++ MSF 7.00\r\n\x1A" "DS\0\0\0"
#define PDB7_SIGNATURE_LEN 32

typedef void (*parse_stream_)(void *stream, R_STREAM_FILE *stream_file);

typedef struct {
	int indx;
	parse_stream_ parse_stream;
	void *stream;
	EStream type;
	free_func free;
} SStreamParseFunc;

///////////////////////////////////////////////////////////////////////////////
static void free_pdb_stream(void *stream) {
	R_PDB_STREAM *pdb_stream = (R_PDB_STREAM *) stream;

	if (pdb_stream) {
		//R_FREE (pdb_stream->pages);
		if (pdb_stream->pages) {
//			free(pdb_stream->pages);
			pdb_stream->pages = 0;
		}
	}
}

//static void pdb_stream_get_data(R_PDB_STREAM *pdb_stream, char *data)
//{
//	int pos = stream_file_tell(&pdb_stream->stream_file);
//	stream_file_seek(&pdb_stream->stream_file, 0, 0);
//	stream_file_read(&pdb_stream->stream_file, -1, data);
//	stream_file_seek(&pdb_stream->stream_file, pos, 0);
//}

///////////////////////////////////////////////////////////////////////////////
/// size - default value = -1
/// page_size - default value = 0x1000
///////////////////////////////////////////////////////////////////////////////
static int init_r_pdb_stream(R_PDB_STREAM *pdb_stream, FILE *fp, int *pages,
		int pages_amount, int index, int size, int page_size) {
	pdb_stream->fp = fp;
	pdb_stream->pages = pages;
	pdb_stream->indx = index;
	pdb_stream->page_size = page_size;
	pdb_stream->pages_amount = pages_amount;

	if (size == -1) {
		pdb_stream->size =  pages_amount * page_size;
	} else {
		pdb_stream->size = size;
	}

	init_r_stream_file (&(pdb_stream->stream_file), fp, pages, pages_amount, size, page_size);
	pdb_stream->free_ = free_pdb_stream;

	return 1;
}

///////////////////////////////////////////////////////////////////////////////
static int read_int_var(char *var_name, int *var, FILE *fp) {
	int bytes_read = fread(var, 4, 1, fp);
	if (bytes_read != 1) {
		eprintf ("error while reading from file [%s]", var_name);
		return 0;
	}
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
static int count_pages(int length, int page_size) {
	int num_pages = 0;
	num_pages = length / page_size;
	if (length % page_size)
		num_pages++;
	return num_pages;
}

///////////////////////////////////////////////////////////////////////////////
static int init_pdb7_root_stream(R_PDB *pdb, int *root_page_list, int pages_amount,
		EStream indx, int root_size, int page_size) {
	int num_streams = 0;
	char *data = 0;
	char *tmp_data = 0;
	int num_pages = 0;
	int i = 0;
	int *sizes = 0;
	int stream_size = 0;
	int pos = 0;
	R_PDB_STREAM *pdb_stream = 0;
	int data_size = 0;
	int tmp_data_max_size = 0;

	char *tmp;

	R_PDB7_ROOT_STREAM *root_stream7;

	pdb->root_stream = (R_PDB7_ROOT_STREAM *) malloc(sizeof(R_PDB7_ROOT_STREAM));
	init_r_pdb_stream(&pdb->root_stream->pdb_stream, pdb->fp, root_page_list, pages_amount,
					  indx, root_size, page_size);

	root_stream7 = pdb->root_stream;
	pdb_stream = &(root_stream7->pdb_stream);

	stream_file_get_size(&pdb_stream->stream_file, &data_size);
	data = (char *) malloc(data_size);
	stream_file_get_data(&pdb_stream->stream_file, data);

	num_streams = *(int *)data;
	tmp_data = data;
	tmp_data += 4;

	root_stream7->num_streams = num_streams;

	tmp_data_max_size = (data_size - (num_streams * 4 - 4));
	if (tmp_data_max_size <= 0) {
		eprintf ("too much amount of streams\n"
			   "curremt pdb file is not correct\n");
		return 0;
	}

	sizes = (int *) calloc (num_streams, 4);
	if (!sizes) {
		eprintf ("too much amount of streams\n"
			   "current pdb file is not correct\n");
		return 0;
	}

	for (i = 0; i < num_streams; i++) {
		stream_size = *(int *)(tmp_data);
		tmp_data += 4;
		if (stream_size == UT32_MAX)
			stream_size = 0;
		memcpy (sizes + i, &stream_size, 4);
	}

//	char *tmp_file_name = (char *) malloc(strlen("/root/test.pdb.000") + 1);
//	short ii;
//	FILE *tmp_file;
	tmp_data = ((char *)data + num_streams * 4 + 4);
	root_stream7->streams_list = r_list_new();
	RList *pList = root_stream7->streams_list;
	SPage *page = 0;
	for (i = 0; i < num_streams; i++) {
		num_pages = count_pages(sizes[i], page_size);

		if ((pos + num_pages) > tmp_data_max_size) {
			eprintf("warning: looks like there is not correct values "
				   "of stream size in pdb file\n");
			break;
		}

		// TODO: cache that num_pages * 4, its used 4 times
		tmp = (char *) calloc (num_pages, 4);
		memset(tmp, 0, num_pages * 4);
		page = (SPage *) malloc (sizeof (SPage));
		if (num_pages != 0) {
			memcpy (tmp, tmp_data + pos, num_pages * 4);
			pos += num_pages * 4;
//			sprintf(tmp_file_name, "%s%d", "/root/test.pdb", i);
//			tmp_file = fopen(tmp_file_name, "wb");
//			fwrite(tmp, num_pages * 4, 1, tmp_file);
//			fclose(tmp_file);
			page->stream_size = sizes[i];
			page->stream_pages = tmp;
			page->num_pages = num_pages;
		} else {
			page->stream_size = 0;
			page->stream_pages = 0;
			page->num_pages = 0;
			free(tmp);
		}

		r_list_append (pList, page);
	}
	free (sizes);
	free (data);
//	printf("init_pdb7_root_stream()\n");
	return 1;
}

///////////////////////////////////////////////////////////////////////////////
// R2: ugly indentation
//static void init_parsed_pdb_stream(SParsedPDBStream *pdb_stream, FILE *fp, int *pages,
//								   int pages_amount, int index, int size,
//								   int page_size, f_load pLoad)
//{
//	pdb_stream->pdb_stream = (R_PDB_STREAM *) malloc(sizeof(R_PDB_STREAM));
//	init_r_pdb_stream(pdb_stream->pdb_stream, fp, pages, pages_amount, index, size, page_size);
//	pdb_stream->load = pLoad;
//	if (pLoad != NULL) {
//		pLoad(pdb_stream, &(pdb_stream->pdb_stream->stream_file));
//	}
//}

///////////////////////////////////////////////////////////////////////////////
static void parse_pdb_info_stream(void *parsed_pdb_stream, R_STREAM_FILE *stream) {
	SPDBInfoStream *tmp = (SPDBInfoStream *)parsed_pdb_stream;

	tmp->names = 0;

	stream_file_read (stream, 4, (char *)&tmp->/*data.*/version);
	stream_file_read (stream, 4, (char *)&tmp->/*data.*/time_date_stamp);
	stream_file_read (stream, 4, (char *)&tmp->/*data.*/age);
	stream_file_read (stream, 4, (char *)&tmp->/*data.*/guid.data1);
	stream_file_read (stream, 2, (char *)&tmp->/*data.*/guid.data2);
	stream_file_read (stream, 2, (char *)&tmp->/*data.*/guid.data3);
	stream_file_read (stream, 8, (char *)&tmp->/*data.*/guid.data4);
	stream_file_read (stream, 4, (char *)&tmp->/*data.*/cb_names);

	tmp->/*data.*/names = (char *) malloc(tmp->/*data.*/cb_names);
	stream_file_read (stream, tmp->/*data.*/cb_names, tmp->/*data.*/names);
}

///////////////////////////////////////////////////////////////////////////////
static void free_info_stream(void *stream) {
	SPDBInfoStream *info_stream = (SPDBInfoStream *)stream;
	free (info_stream->names);
}

///////////////////////////////////////////////////////////////////////////////
#define ADD_INDX_TO_LIST(list, index, stream_size, stream_type, free_func, parse_func) { \
	if (index != -1) { \
		SStreamParseFunc *stream_parse_func = (SStreamParseFunc *) malloc(sizeof(SStreamParseFunc)); \
		stream_parse_func->indx = (index); \
		stream_parse_func->type = (stream_type); \
		stream_parse_func->parse_stream = (parse_func); \
		stream_parse_func->free = (free_func); \
		if (stream_size) { \
			stream_parse_func->stream = malloc(stream_size); \
		} else { \
			stream_parse_func->stream = 0; \
		} \
		r_list_append((list), stream_parse_func); \
	} \
}

///////////////////////////////////////////////////////////////////////////////
static void fill_list_for_stream_parsing(RList *l, SDbiStream *dbi_stream) {
	ADD_INDX_TO_LIST(l, dbi_stream->dbi_header.symrecStream, sizeof(SGDATAStream),
					 ePDB_STREAM_GSYM, free_gdata_stream, parse_gdata_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_section_hdr, sizeof(SPEStream),
					 ePDB_STREAM_SECT_HDR, free_pe_stream, parse_pe_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_section_hdr_orig, sizeof(SPEStream),
					 ePDB_STREAM_SECT__HDR_ORIG, free_pe_stream, parse_pe_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_omap_to_src, sizeof(SOmapStream),
					 ePDB_STREAM_OMAP_TO_SRC, free_omap_stream, parse_omap_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_omap_from_src, sizeof(SOmapStream),
					 ePDB_STREAM_OMAP_FROM_SRC, free_omap_stream, parse_omap_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_fpo, sizeof(SFPOStream),
					 ePDB_STREAM_FPO, free_fpo_stream, parse_fpo_stream);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_new_fpo, sizeof(SFPONewStream),
					 ePDB_STREAM_FPO_NEW, free_fpo_stream, parse_fpo_new_stream);

	// unparsed, but know their names
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_xdata, 0, ePDB_STREAM_XDATA, 0, 0);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_pdata, 0, ePDB_STREAM_PDATA, 0, 0);
	ADD_INDX_TO_LIST(l, dbi_stream->dbg_header.sn_token_rid_map, 0, ePDB_STREAM_TOKEN_RID_MAP, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////
static void find_indx_in_list(RList *l, int index, SStreamParseFunc **res) {
	SStreamParseFunc *stream_parse_func = 0;
	RListIter *it = 0;

	*res = 0;
	it = r_list_iterator(l);
	while (r_list_iter_next(it)) {
		stream_parse_func = (SStreamParseFunc *) r_list_iter_get(it);
		if (index == stream_parse_func->indx) {
			*res = stream_parse_func;
			return;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
static int pdb_read_root(R_PDB *pdb) {
	int i = 0;

	RList *pList = pdb->pdb_streams;
	R_PDB7_ROOT_STREAM *root_stream = pdb->root_stream;
	R_PDB_STREAM *pdb_stream = 0;
	SPDBInfoStream *pdb_info_stream = 0;
	STpiStream *tpi_stream = 0;
	R_STREAM_FILE stream_file;
	RListIter *it;
	SPage *page = 0;
	SStreamParseFunc *stream_parse_func = 0;

	it = r_list_iterator(root_stream->streams_list);
	while (r_list_iter_next(it)) {
		page = (SPage*) r_list_iter_get(it);
		init_r_stream_file(&stream_file, pdb->fp, (int *)page->stream_pages,
						   page->num_pages/*root_stream->pdb_stream.pages_amount*/,
						   page->stream_size,
						   root_stream->pdb_stream.page_size);
		switch (i) {
		//TODO: rewrite for style like for streams from dbg stream
		//      look default
		case ePDB_STREAM_PDB:
			pdb_info_stream = (SPDBInfoStream *) malloc(sizeof(SPDBInfoStream));
			pdb_info_stream->free_ = free_info_stream;
			parse_pdb_info_stream(pdb_info_stream, &stream_file);
			r_list_append(pList, pdb_info_stream);
			break;
		case ePDB_STREAM_TPI:
			tpi_stream = (STpiStream *) malloc(sizeof(STpiStream));
			init_tpi_stream(tpi_stream);
			parse_tpi_stream(tpi_stream, &stream_file);
			r_list_append(pList, tpi_stream);
			break;
		case ePDB_STREAM_DBI:
		{
			SDbiStream *dbi_stream = (SDbiStream *) malloc(sizeof(SDbiStream));
			init_dbi_stream(dbi_stream);
			parse_dbi_stream(dbi_stream, &stream_file);
			r_list_append(pList, dbi_stream);
			pdb->pdb_streams2 = r_list_new();
			fill_list_for_stream_parsing(pdb->pdb_streams2, dbi_stream);
			break;
		}
		default:
			find_indx_in_list(pdb->pdb_streams2, i, &stream_parse_func);
			if (stream_parse_func) {
				if (stream_parse_func->parse_stream) {
					stream_parse_func->parse_stream(stream_parse_func->stream,
													&stream_file);
					break;
				}
			}

			pdb_stream = (R_PDB_STREAM *) malloc(sizeof(R_PDB_STREAM));
			init_r_pdb_stream(pdb_stream, pdb->fp, (int *)page->stream_pages,
							  root_stream->pdb_stream.pages_amount, i,
							  page->stream_size,
							  root_stream->pdb_stream.page_size);
			r_list_append(pList, pdb_stream);

			break;
		}

		if (stream_file.error) {
			return 0;
		}

		i++;
	}

	return 1;
}

///////////////////////////////////////////////////////////////////////////////
static int pdb7_parse(R_PDB *pdb) {
	char signature[PDB7_SIGNATURE_LEN + 1];
	int num_root_index_pages = 0;
	int *root_index_pages = 0;
	void *root_page_data = 0;
	int *root_page_list = 0;
	int num_root_pages = 0;
	int num_file_pages = 0;
	int alloc_tbl_ptr = 0;
	int bytes_read = 0;
	int page_size = 0;
	int root_size = 0;
	int reserved = 0;
	void *p_tmp;
	int i = 0;

	bytes_read = fread (signature, 1, PDB7_SIGNATURE_LEN, pdb->fp);
	if (bytes_read != PDB7_SIGNATURE_LEN) {
		eprintf ("error while reading PDB7_SIGNATURE\n");
		goto error;
	}

	if (!read_int_var ("page_size", &page_size, pdb->fp))
		goto error;
	if (!read_int_var ("alloc_tbl_ptr", &alloc_tbl_ptr, pdb->fp))
		goto error;
	if (!read_int_var ("num_file_pages", &num_file_pages, pdb->fp))
		goto error;
	if (!read_int_var ("root_size", &root_size, pdb->fp))
		goto error;
	if (!read_int_var("reserved", &reserved, pdb->fp))
		goto error;

	// FIXME: why they is not equal ????
//	if (memcmp(signature, PDB7_SIGNATURE, PDB7_SIGNATURE_LEN) != 0) {
//		printf("Invalid signature for PDB7 format\n");
//		//goto error;
//	}

	num_root_pages = count_pages(root_size, page_size);
	num_root_index_pages = count_pages((num_root_pages * 4), page_size);

	root_index_pages = (int *)calloc (sizeof(int), num_root_index_pages);
	if (!root_index_pages) {
		eprintf("error memory allocation\n");
		goto error;
	}

	bytes_read = fread(root_index_pages, 4, num_root_index_pages, pdb->fp);
	if (bytes_read != num_root_index_pages) {
		eprintf ("error while reading root_index_pages\n");
		goto error;
	}

	root_page_data = (int *)malloc(page_size * num_root_index_pages);
	if (!root_page_data) {
		eprintf ("error memory allocation of root_page_data\n");
		goto error;
	}

	p_tmp = root_page_data;
	for (i = 0; i < num_root_index_pages; i++) {
		fseek(pdb->fp, root_index_pages[i] * page_size, SEEK_SET);
		fread(p_tmp, page_size, 1, pdb->fp);
		p_tmp = (char *)p_tmp + page_size;
	}

	root_page_list = (int *)calloc (sizeof(int), num_root_pages);
	if (!root_page_list) {
		eprintf ("error: memory allocation of root page\n");
		goto error;
	}

	p_tmp = root_page_data;
	for (i = 0; i < num_root_pages; i++) {
		root_page_list[i] = *((int *)p_tmp);
		p_tmp = (int *)p_tmp + 1;
	}

	pdb->pdb_streams2 = 0;
	if (!init_pdb7_root_stream (pdb, root_page_list, num_root_pages,
			ePDB_STREAM_ROOT, root_size, page_size)) {
		eprintf ("root stream has not initialized\n");
		goto error;
	}
	if (!pdb_read_root (pdb)) {
		eprintf ("pdb root has not initialized\n");
		goto error;
	}

	R_FREE (root_page_list);
	R_FREE (root_page_data);
	R_FREE (root_index_pages);
	return 1;

error:
	R_FREE (root_page_list);
	R_FREE (root_page_data);
	R_FREE (root_index_pages);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
static void finish_pdb_parse(R_PDB *pdb)
{
	R_PDB7_ROOT_STREAM *p = pdb->root_stream;

	RListIter *it;
	SPage *page = 0;

	it = r_list_iterator(p->streams_list);
	while (r_list_iter_next(it)) {
		page = (SPage *) r_list_iter_get(it);
		free(page->stream_pages);
		page->stream_pages = 0;
		free(page);
		page = 0;
	}
	r_list_free(p->streams_list);
	p->streams_list = 0;
	free(p);
	p = 0;
	// end of free of R_PDB7_ROOT_STREAM

	// TODO: maybe create some kind of destructor?
	// free of pdb->pdb_streams
//	SParsedPDBStream *parsed_pdb_stream = 0;
	SPDBInfoStream *pdb_info_stream = 0;
	STpiStream *tpi_stream = 0;
	SDbiStream *dbi_stream = 0;
	SStreamParseFunc *stream_parse_func;
	R_PDB_STREAM *pdb_stream = 0;
	int i = 0;
	it = r_list_iterator(pdb->pdb_streams);
	while (r_list_iter_next(it)) {
		switch (i) {
		case 1:
			pdb_info_stream = (SPDBInfoStream *) r_list_iter_get(it);
			pdb_info_stream->free_(pdb_info_stream);
			free(pdb_info_stream);
			break;
		case 2:
			tpi_stream = (STpiStream *) r_list_iter_get(it);
			tpi_stream->free_(tpi_stream);
			free(tpi_stream);
			break;
		case 3:
			dbi_stream = (SDbiStream *) r_list_iter_get(it);
			dbi_stream->free_(dbi_stream);
			free(dbi_stream);
			break;
		default:
			find_indx_in_list(pdb->pdb_streams2, i, &stream_parse_func);
			if (stream_parse_func) {
				break;
			}

			pdb_stream = (R_PDB_STREAM *) r_list_iter_get(it);
			pdb_stream->free_(pdb_stream);
			free(pdb_stream);
			break;
		}

		i++;
	}
	r_list_free(pdb->pdb_streams);
	// enf of free of pdb->pdb_streams

	// start of free pdb->pdb_streams2
	it = r_list_iterator(pdb->pdb_streams2);
	while (r_list_iter_next(it)) {
		stream_parse_func = (SStreamParseFunc *) r_list_iter_get(it);
		if (stream_parse_func->free) {
			stream_parse_func->free(stream_parse_func->stream);
			free(stream_parse_func->stream);
		}
		free(stream_parse_func);
	}
	r_list_free(pdb->pdb_streams2);
	// end of free pdb->streams2

	if (pdb->stream_map)
		free(pdb->stream_map);

	fclose(pdb->fp);
//	printf("finish_pdb_parse()\n");
}

///////////////////////////////////////////////////////////////////////////////
static void print_types(R_PDB *pdb) {
//	printf("print_types()\n");
	char *name;
	int val = 0;
	int offset = 0;
	SType *t = 0;
	STypeInfo *tf = 0;
	RListIter *it = 0, *it2 = 0;
	RList *plist = pdb->pdb_streams, *ptmp;
	STpiStream *tpi_stream = r_list_get_n (plist, ePDB_STREAM_TPI);

	if (!tpi_stream) {
		eprintf ("there is no tpi stream in current pdb\n");
		return;
	}

	it = r_list_iterator(tpi_stream->types);
	while (r_list_iter_next(it)) {
		val = 0;
		t = (SType *) r_list_iter_get(it);
		tf = &t->type_data;
		if ((tf->leaf_type == eLF_STRUCTURE) || (tf->leaf_type == eLF_UNION)
			|| (tf->leaf_type == eLF_ENUM)) {

			if (tf->is_fwdref) {
				tf->is_fwdref(tf, &val);
				if (val == 1) {
					continue;
				}
			}

			if (tf->get_name)
				tf->get_name(tf, &name);
			// val for STRUCT or UNION mean size
			if (tf->get_val)
				tf->get_val(tf, &val);
			pdb->printf ("%s: size 0x%x\n", name, val);

			if (tf->get_members)
				tf->get_members(tf, &ptmp);
			it2 = r_list_iterator(ptmp);
			while (r_list_iter_next(it2)) {
				tf = (STypeInfo *) r_list_iter_get(it2);
				if (tf->get_name)
					tf->get_name(tf, &name);
				if (tf->get_val)
					tf->get_val(tf, &offset);
				else
					offset = 0;
				pdb->printf ("  0x%x: %s type:", offset, name);
				if (tf->get_print_type)
					tf->get_print_type(tf, &name);
				pdb->printf ("%s\n", name);
				free (name);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
static void print_gvars(R_PDB *pdb, int img_base) {
	RList *l = 0;
	RListIter *it = 0;
	SStreamParseFunc *omap = 0, *sctns = 0, *sctns_orig = 0 ,
			*gsym = 0, *tmp = 0;
	SGDATAStream *gsym_data_stream = 0;
	SGlobal *gdata = 0;
	SPEStream *pe_stream = 0;
	SIMAGE_SECTION_HEADER *sctn_header = 0;

	l = pdb->pdb_streams2;
	it = r_list_iterator(l);
	while (r_list_iter_next(it)) {
		tmp = (SStreamParseFunc *) r_list_iter_get(it);
		switch (tmp->type) {
		case ePDB_STREAM_SECT__HDR_ORIG:
			sctns_orig = tmp;
			break;
		case ePDB_STREAM_SECT_HDR:
			sctns = tmp;
			break;
		case ePDB_STREAM_OMAP_FROM_SRC:
			omap = tmp;
			break;
		case ePDB_STREAM_GSYM:
			gsym = tmp;
			break;
		default:
			break;
		}
	}

	if (!gsym) {
		eprintf ("there is no global symbols in current pdb\n");
		return;
	}

	gsym_data_stream = (SGDATAStream *) gsym->stream;
	if ((omap != 0) && (sctns_orig != 0)) {
		pe_stream = (SPEStream *) sctns_orig->stream;
	} else {
		pe_stream = (SPEStream *) sctns->stream;
	}

	it = r_list_iterator(gsym_data_stream->globals_list);
	while (r_list_iter_next(it)) {
		gdata = (SGlobal *) r_list_iter_get(it);
		sctn_header = r_list_get_n(pe_stream->sections_hdrs, (gdata->segment -1));
		if (sctn_header) {
			pdb->printf ("%s, 0x%x, %d, %s\n", gdata->name.name,
				img_base + omap_remap((omap) ? (omap->stream) : 0,
				gdata->offset + sctn_header->virtual_address),
				gdata->symtype, sctn_header->name);
		} else {
			eprintf ("Skipping %s, segment %d does not exist\n",
				   gdata->name.name, (gdata->segment -1));
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
int init_pdb_parser(R_PDB *pdb, const char *filename) {
	char *signature = NULL;
	int bytes_read = 0;

	if (!pdb) {
		eprintf ("struct R_PDB is not correct\n");
		goto error;
	}
	if (!pdb->printf)
		pdb->printf = (PrintfCallback)printf;

	// TODO: Reimplement using RBuffer here
	pdb->fp = r_sandbox_fopen (filename, "rb");
	if (!pdb->fp) {
		eprintf ("file %s can not be open\n", filename);
		goto error;
	}

	signature = (char *)malloc (PDB7_SIGNATURE_LEN);
	if (!signature) {
		eprintf ("memory allocation error\n");
		goto error;
	}

	bytes_read = fread (signature, 1, PDB7_SIGNATURE_LEN, pdb->fp);
	if (bytes_read != PDB7_SIGNATURE_LEN) {
		eprintf ("file reading error\n");
		goto error;
	}

	fseek (pdb->fp, 0, SEEK_SET);

	if (!memcmp (signature, PDB7_SIGNATURE, PDB7_SIGNATURE_LEN)) {
		pdb->pdb_parse = pdb7_parse;
	} else {
		goto error;
	}

	R_FREE (signature);

	pdb->pdb_streams = r_list_new ();
	pdb->stream_map = 0;
	pdb->finish_pdb_parse = finish_pdb_parse;
	pdb->print_types = print_types;
	pdb->print_gvars = print_gvars;
//	printf("init_pdb_parser() finish with success\n");
	return 1;

error:
	R_FREE (signature);

	return 0;
}
