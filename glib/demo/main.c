/* 
 * Copyright (C) 2007 Carlos Garcia Campos  <carlosgc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <poppler.h>
#include <string.h>

#include "info.h"
#include "fonts.h"
#include "render.h"
#include "page.h"
#include "outline.h"
#include "links.h"

enum {
	PGD_TITLE_COLUMN,
	PGD_NPAGE_COLUMN,
	PGD_WIDGET_COLUMN,
	N_COLUMNS
};

typedef struct {
	const gchar *name;
	GtkWidget   *(* create_widget) (PopplerDocument *document);
} PopplerGlibDemo;

static const PopplerGlibDemo demo_list[] = {
	{ "Info",      pgd_info_create_widget },
	{ "Fonts",     pgd_fonts_create_widget },
	{ "Render",    pgd_render_create_widget },
	{ "Page Info", pgd_page_create_widget },
	{ "Outline",   pgd_outline_create_widget },
	{ "Links",     pgd_links_create_widget }
};

static void
pgd_demo_changed (GtkTreeSelection *selection,
		  GtkNotebook      *notebook)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gint n_page;
		
		gtk_tree_model_get (model, &iter,
				    PGD_NPAGE_COLUMN, &n_page,
				    -1);
		gtk_notebook_set_current_page (notebook, n_page);
	}
}

GtkWidget *
pgd_demo_list_create (void)
{
	GtkWidget       *treeview;
	GtkListStore    *model;
	GtkCellRenderer *renderer;
	gint             i;

	model = gtk_list_store_new (N_COLUMNS,
				    G_TYPE_STRING,
				    G_TYPE_INT,
				    G_TYPE_POINTER);
	treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
						     0, "Demos",
						     renderer,
						     "text", PGD_TITLE_COLUMN,
						     NULL);
	
	for (i = 0; i < G_N_ELEMENTS (demo_list); i++) {
		GtkTreeIter iter;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter,
				    PGD_TITLE_COLUMN, demo_list[i].name,
				    PGD_NPAGE_COLUMN, i,
				    -1);
	}

	g_object_unref (model);
	
	return treeview;
}

GtkWidget *
pdg_demo_notebook_create (PopplerDocument *document)
{
	GtkWidget *notebook;
	gint       i;

	notebook = gtk_notebook_new ();
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
	gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
	
	for (i = 0; i < G_N_ELEMENTS (demo_list); i++) {
		GtkWidget *demo_widget;

		demo_widget = demo_list[i].create_widget (document);
		gtk_notebook_append_page (GTK_NOTEBOOK (notebook), demo_widget, NULL);
		gtk_widget_show (demo_widget);
	}

	return notebook;
}

gint main (gint argc, gchar **argv)
{
	PopplerDocument  *document;
	GtkWidget        *win;
	GtkWidget        *hbox;
	GtkWidget        *notebook;
	GtkWidget        *treeview;
	GtkTreeSelection *selection;
	gchar            *uri;
	GTimer           *timer;
	GError           *error = NULL;

	if (argc != 2) {
		g_print ("Usage: poppler-glib-demo FILE\n");
		return 1;
	}

	gtk_init (&argc, &argv);

	if (g_ascii_strncasecmp (argv[1], "file://", strlen ("file://")) == 0) {
		uri = g_strdup (argv[1]);
	} else {
		uri = g_filename_to_uri (argv[1], NULL, &error);
		if (error) {
			g_print ("Error: %s\n", error->message);
			g_error_free (error);
		
			return 1;
		}
	}
	
	timer = g_timer_new ();
	document = poppler_document_new_from_file (uri, NULL, &error);
	g_timer_stop (timer);
	if (error) {
		g_print ("Error: %s\n", error->message);
		g_error_free (error);
		g_free (uri);

		return 1;
	}

	g_free (uri);

	g_print ("Document successfully loaded in %.4f seconds\n",
		 g_timer_elapsed (timer, NULL));
	g_timer_destroy (timer);

	/* Main window */
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (win), 600, 600);
	gtk_window_set_title (GTK_WINDOW (win), "Poppler GLib Demo");
	g_signal_connect (G_OBJECT (win), "delete-event",
			  G_CALLBACK (gtk_main_quit), NULL);

	hbox = gtk_hbox_new (FALSE, 6);

	treeview = pgd_demo_list_create ();
	gtk_box_pack_start (GTK_BOX (hbox), treeview, FALSE, TRUE, 0);
	gtk_widget_show (treeview);
	
	notebook = pdg_demo_notebook_create (document);
	gtk_box_pack_start (GTK_BOX (hbox), notebook, TRUE, TRUE, 0);
	gtk_widget_show (notebook);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	g_signal_connect (G_OBJECT (selection), "changed",
			  G_CALLBACK (pgd_demo_changed),
			  (gpointer) notebook);

	gtk_container_add (GTK_CONTAINER (win), hbox);
	gtk_widget_show (hbox);
	
	gtk_widget_show (win);

	gtk_main ();

	g_object_unref (document);
	
	return 0;
}
