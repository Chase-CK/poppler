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

#include "page.h"
#include "utils.h"

typedef struct {
	PopplerDocument *doc;

	GtkWidget       *index;
	GtkWidget       *label;
	GtkWidget       *size;
	GtkWidget       *duration;
	GtkWidget       *thumbnail;
	GtkWidget       *thumbnail_size;

	gint             page;
} PgdPageDemo;

static void
pgd_page_free (PgdPageDemo *demo)
{
	if (!demo)
		return;

	if (demo->doc) {
		g_object_unref (demo->doc);
		demo->doc = NULL;
	}

	g_free (demo);
}

static void
pgd_page_set_page (PgdPageDemo *demo,
		   PopplerPage *page)
{
	GdkPixbuf *thumbnail;
	gchar     *str;

	str = page ? g_strdup_printf ("%d", poppler_page_get_index (page)) : NULL;
	gtk_label_set_text (GTK_LABEL (demo->index), str);
	g_free (str);

	if (page) {
		g_object_get (G_OBJECT (page), "label", &str, NULL);
		gtk_label_set_text (GTK_LABEL (demo->label), str);
		g_free (str);
	} else {
		gtk_label_set_text (GTK_LABEL (demo->label), NULL);
	}

	if (page) {
		gdouble width, height;

		poppler_page_get_size (page, &width, &height);
		str = g_strdup_printf ("%.f2 x %.f2", width, height);
		gtk_label_set_text (GTK_LABEL (demo->size), str);
		g_free (str);
	} else {
		gtk_label_set_text (GTK_LABEL (demo->size), NULL);
	}

	str = page ? g_strdup_printf ("%d seconds", poppler_page_get_duration (page)) : NULL;
	gtk_label_set_text (GTK_LABEL (demo->duration), str);
	g_free (str);

	thumbnail = page ? poppler_page_get_thumbnail (page) : NULL;
	if (thumbnail) {
		gint width, height;
		
		poppler_page_get_thumbnail_size (page, &width, &height);
		str = g_strdup_printf ("%d x %d", width, height);
		gtk_label_set_text (GTK_LABEL (demo->thumbnail_size), str);
		g_free (str);

		gtk_image_set_from_pixbuf (GTK_IMAGE (demo->thumbnail), thumbnail);
		g_object_unref (thumbnail);
	} else {
		str = g_strdup ("<i>No thumbnail found</i>");
		gtk_label_set_markup (GTK_LABEL (demo->thumbnail_size), str);
		g_free (str);

		gtk_image_set_from_stock (GTK_IMAGE (demo->thumbnail),
					  GTK_STOCK_MISSING_IMAGE,
					  GTK_ICON_SIZE_DIALOG);
	}
}

static void
pgd_page_get_info (GtkWidget   *button,
		   PgdPageDemo *demo)
{
	PopplerPage *page;

	page = poppler_document_get_page (demo->doc, demo->page);
	pgd_page_set_page (demo, page);
	g_object_unref (page);
}

static void
pgd_page_page_selector_value_changed (GtkSpinButton *spinbutton,
				      PgdPageDemo   *demo)
{
	demo->page = (gint)gtk_spin_button_get_value (spinbutton) - 1;
}

GtkWidget *
pgd_page_create_widget (PopplerDocument *document)
{
	PgdPageDemo *demo;
	GtkWidget   *vbox;
	GtkWidget   *hbox, *page_selector;
	GtkWidget   *button;
	GtkWidget   *frame, *alignment;
	GtkWidget   *table;
	GtkWidget   *label;
	GtkWidget   *thumnail_box;
	gchar       *str;
	gint         n_pages;
	gint         row = 0;

	demo = g_new0 (PgdPageDemo, 1);

	demo->doc = g_object_ref (document);

	n_pages = poppler_document_get_n_pages (document);

	vbox = gtk_vbox_new (FALSE, 12);

	hbox = gtk_hbox_new (FALSE, 6);

	label = gtk_label_new ("Page:");
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	gtk_widget_show (label);

	page_selector = gtk_spin_button_new_with_range (1, n_pages, 1);
	g_signal_connect (G_OBJECT (page_selector), "value-changed",
			  G_CALLBACK (pgd_page_page_selector_value_changed),
			  (gpointer)demo);
	gtk_box_pack_start (GTK_BOX (hbox), page_selector, FALSE, TRUE, 0);
	gtk_widget_show (page_selector);

	str = g_strdup_printf ("of %d", n_pages);
	label = gtk_label_new (str);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	gtk_widget_show (label);
	g_free (str);

	button = gtk_button_new_with_label ("Get Info");
	g_signal_connect (G_OBJECT (button), "clicked",
			  G_CALLBACK (pgd_page_get_info),
			  (gpointer)demo);
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	gtk_widget_show (button);

	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
	gtk_widget_show (hbox);

	hbox = gtk_hbox_new (FALSE, 6);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), "<b>Page Properties</b>");
	gtk_frame_set_label_widget (GTK_FRAME (frame), label);
	gtk_widget_show (label);

	alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 5, 5, 12, 5);
	gtk_container_add (GTK_CONTAINER (frame), alignment);
	gtk_widget_show (alignment);

	table = gtk_table_new (3, 2, FALSE);

	gtk_table_set_col_spacings (GTK_TABLE (table), 6);
	gtk_table_set_row_spacings (GTK_TABLE (table), 6);

	pgd_table_add_property_with_value_widget (GTK_TABLE (table), "<b>Page Index:</b>",
						  &(demo->index), NULL, &row);
	pgd_table_add_property_with_value_widget (GTK_TABLE (table), "<b>Page Label:</b>",
						  &(demo->label), NULL, &row);
	pgd_table_add_property_with_value_widget (GTK_TABLE (table), "<b>Page Size:</b>",
						  &(demo->size), NULL, &row);
	pgd_table_add_property_with_value_widget (GTK_TABLE (table), "<b>Page Duration:</b>",
						  &(demo->duration), NULL, &row);

	gtk_container_add (GTK_CONTAINER (alignment), table);
	gtk_widget_show (table);

	gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
	gtk_widget_show (frame);

	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	gtk_frame_set_label_align (GTK_FRAME (frame), 0.5, 0.5);
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), "<b>Page Thumbnail</b>");
	gtk_frame_set_label_widget (GTK_FRAME (frame), label);
	gtk_widget_show (label);

	alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 5, 5, 12, 5);
	gtk_container_add (GTK_CONTAINER (frame), alignment);
	gtk_widget_show (alignment);
	
	thumnail_box = gtk_vbox_new (FALSE, 6);
	
	demo->thumbnail = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (thumnail_box), demo->thumbnail, TRUE, TRUE, 0);
	gtk_widget_show (demo->thumbnail);
	
	demo->thumbnail_size = gtk_label_new (NULL);
	g_object_set (G_OBJECT (demo->thumbnail_size), "xalign", 0.5, NULL);
	gtk_box_pack_start (GTK_BOX (thumnail_box), demo->thumbnail_size, TRUE, TRUE, 0);
	gtk_widget_show (demo->thumbnail_size);

	gtk_container_add (GTK_CONTAINER (alignment), thumnail_box);
	gtk_widget_show (thumnail_box);

	gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
	gtk_widget_show (frame);
	
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
	gtk_widget_show (hbox);

	g_object_weak_ref (G_OBJECT (vbox),
			   (GWeakNotify)pgd_page_free,
			   (gpointer)demo);
	
	return vbox;
}
