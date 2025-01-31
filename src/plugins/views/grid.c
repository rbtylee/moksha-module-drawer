#include "grid.h"
#include "math.h"

/* Local Structures */
typedef struct _Instance Instance;
typedef struct _Item Item;

typedef enum
{
   GRID_TOP,
   GRID_RIGHT,
   GRID_BOTTOM,
   GRID_LEFT,
   GRID_FLOAT
} Grid_Orient;

struct _Instance
{
   Drawer_View          *view;
   Evas                 *evas;
   Eina_List            *items;
   Evas_Object          *o_box, *o_con, *o_scroll;
   char                  theme_file[PATH_MAX];
   const char           *parent_id;
   Grid_Orient           orient;
};

struct _Item
{
   Instance             *inst;
   Evas_Object          *o_holder, *o_icon;
   Drawer_Source_Item   *si;
   Eina_Bool             isa_category;
};

static void _grid_reconfigure(Instance *inst);
static void _grid_containers_create(Instance *inst);
static Item *_grid_item_create(Instance *inst, Drawer_Source_Item *si);
static Item *_grid_category_create(Instance *inst, Drawer_Source_Item *si);
static void _grid_items_free(Instance *inst);

static int  _grid_sort_by_category_cb(const void *d1, const void *d2);
static void _grid_entry_select_cb(void *data, Evas_Object *obj, const char *emission __UNUSED__,
                                  const char *source __UNUSED__);
static void _grid_entry_deselect_cb(void *data, Evas_Object *obj, const char *emission __UNUSED__,
                                    const char *source __UNUSED__);
static void _grid_entry_activate_cb(void *data, Evas_Object *obj, const char *emission __UNUSED__,
                                    const char *source __UNUSED__);
static void _grid_entry_context_cb(void *data, Evas_Object *obj, const char *emission __UNUSED__,
                                   const char *source __UNUSED__);

static void _grid_event_activate_free(void *data __UNUSED__, void *event);
static void _grid_event_context_free(void *data __UNUSED__, void *event);

EAPI Drawer_Plugin_Api drawer_plugin_api = { DRAWER_PLUGIN_API_VERSION, "Grid" };

EAPI void *
drawer_plugin_init(Drawer_Plugin *p, const char *id)
{
   Instance *inst = E_NEW(Instance, 1);

   inst->view = DRAWER_VIEW(p);
   inst->parent_id = eina_stringshare_add(id);
   snprintf(inst->theme_file, sizeof(inst->theme_file),
            "%s/e-module-drawer.edj", drawer_module_dir_get());

   return inst;
}

EAPI int
drawer_plugin_shutdown(Drawer_Plugin *p)
{
   Instance *inst = p->data;

   _grid_items_free(inst);
   eina_stringshare_del(inst->parent_id);
   if (inst->o_box) evas_object_del(inst->o_box);
   if (inst->o_con) evas_object_del(inst->o_con);
   if (inst->o_scroll) evas_object_del(inst->o_scroll);

   E_FREE(inst);

   return 1;
}

EAPI Evas_Object *
drawer_view_render(Drawer_View *v, Evas *evas, Eina_List *items)
{
   Instance *inst = NULL;
   Eina_List *l = NULL, *ll = NULL;
   Drawer_Source_Item *si;
   const char *cat = NULL;
   Eina_Bool change = EINA_FALSE;
   Item *e;

   inst = DRAWER_PLUGIN(v)->data;

   inst->evas = evas;

   _grid_items_free(inst);

   if (inst->o_box) evas_object_del(inst->o_box);
   if (inst->o_con) evas_object_del(inst->o_con);
   if (inst->o_scroll) evas_object_del(inst->o_scroll);
   if (!items) return NULL;
   _grid_containers_create(inst);

   EINA_LIST_FOREACH(items, l, si)
      ll = eina_list_append(ll, si);
   ll = eina_list_sort(ll, eina_list_count(ll), _grid_sort_by_category_cb);
   switch (inst->orient)
     {
        case GRID_BOTTOM:
        case GRID_RIGHT:
           ll = eina_list_reverse(ll);
           break;
        default:
           break;
     }

   EINA_LIST_FOREACH(ll, l, si)
     {
       if (!cat && si->category)
          {
             cat = eina_stringshare_add(si->category);
             change = EINA_TRUE;
          } else if (cat && !si->category)
          {
             eina_stringshare_del(cat);
             cat = NULL;
             change = EINA_TRUE;
          } else if (cat && si->category && (strcmp(cat, si->category)))
          {
             eina_stringshare_del(cat);
             cat = eina_stringshare_add(si->category);
             change = EINA_TRUE;
          } else
            change = EINA_FALSE;

        if (change)
          {
             e = _grid_category_create(inst, si);
             inst->items = eina_list_append(inst->items, e);
             edje_object_part_box_append(inst->o_box, "e.box.content", e->o_holder);
          }
        e = _grid_item_create(inst, si);
        inst->items = eina_list_append(inst->items, e);
        edje_object_part_box_append(inst->o_box, "e.box.content", e->o_holder);
     }
   eina_stringshare_del(cat);

   inst->o_scroll = e_scrollframe_add(evas);
   e_scrollframe_child_set(inst->o_scroll, inst->o_box);
   edje_object_part_swallow(inst->o_con, "e.swallow.content", inst->o_scroll);
   if (!e_scrollframe_custom_theme_set(inst->o_scroll, "base/theme/modules/drawer",
                                       "modules/drawer/grid/scrollframe"))
      e_scrollframe_custom_edje_file_set(inst->o_scroll, inst->theme_file, "modules/drawer/grid/scrollframe");
   evas_object_show(inst->o_scroll);

   _grid_reconfigure(inst);

   return inst->o_con;
}

EAPI void
drawer_view_container_resized(Drawer_View *v)
{
   Instance *inst;
   Evas_Coord vw, vh, mw, mh, w, h, cath = 0;
   Eina_Bool resize = EINA_FALSE;
   Eina_List *l;
   Item *e;

   inst = DRAWER_PLUGIN(v)->data;

calculate:
   e_scrollframe_child_viewport_size_get(inst->o_scroll, &vw, &vh);
   evas_object_resize(inst->o_box, vw + 1, vh + 1);
   /* XXX: switch to size_min_calc when it starts working
    *
    * edje_object_size_min_calc(inst->o_box, &ww, &hh);
    *
    */
   evas_object_size_hint_min_get(edje_object_part_object_get(inst->o_box, "e.box.content"), &mw, &mh);
   evas_object_geometry_get(inst->o_box, NULL, NULL, &w, &h);

   if (vw >= mw)
     {
        if (w != vw)
          {
             w = vw;
             resize = EINA_TRUE;
          }
     } else if (w != mw)
     {
        w = mw;
        resize = EINA_TRUE;
     }

   if (vh >= mh)
     {
        if (h != vh)
          {
             h = vh;
             resize = EINA_TRUE;
          }
     } else if (h != mh)
     {
        h = mh;
        resize = EINA_TRUE;
     }

   if (resize) evas_object_resize(inst->o_box, w, h);
   if (inst->items && !cath)
     {
        EINA_LIST_FOREACH(inst->items, l, e)
          {
             if (e->isa_category)
               {
                  if (!cath)
                     edje_object_size_max_get(e->o_holder, NULL, &cath);
                  evas_object_resize(e->o_holder, w - 1, cath);
               }
          }
          if (cath) goto calculate;
       }
}

EAPI void
drawer_view_orient_set(Drawer_View *v, E_Gadcon_Orient orient)
{
   Instance *inst = DRAWER_PLUGIN(v)->data;

   switch (orient)
     {
        case E_GADCON_ORIENT_CORNER_RT:
        case E_GADCON_ORIENT_CORNER_RB:
        case E_GADCON_ORIENT_RIGHT:
           inst->orient = GRID_RIGHT;
           break;
        case E_GADCON_ORIENT_LEFT:
        case E_GADCON_ORIENT_CORNER_LT:
        case E_GADCON_ORIENT_CORNER_LB:
           inst->orient = GRID_LEFT;
           break;
        case E_GADCON_ORIENT_TOP:
        case E_GADCON_ORIENT_CORNER_TL:
        case E_GADCON_ORIENT_CORNER_TR:
           inst->orient = GRID_TOP;
           break;
        case E_GADCON_ORIENT_BOTTOM:
        case E_GADCON_ORIENT_CORNER_BL:
        case E_GADCON_ORIENT_CORNER_BR:
           inst->orient = GRID_BOTTOM;
           break;
        case E_GADCON_ORIENT_FLOAT:
           inst->orient = GRID_FLOAT;
           break;
        default:
           break;
     }
}

EAPI void
drawer_view_toggle_visibility(Drawer_View *v, Eina_Bool show)
{
   Instance *inst = DRAWER_PLUGIN(v)->data;

   if (show)
      edje_object_signal_emit(inst->o_box, "e,action,show", "drawer");
   else
      edje_object_signal_emit(inst->o_box, "e,action,hide", "drawer");
}

static void
_grid_reconfigure(Instance *inst)
{
   // Evas_Coord zw;
   Evas_Coord cath = 0, catw = 0, ew = 0, eh = 0, cw = 0, ch = 0, w = 0, h = 0;
   // E_Zone *zone = e_util_zone_current_get(e_manager_current_get());
   Eina_List *l;
   Item *e;
   int max_item_count = 0, item_count = 0, cat_count = 0; // iter_count;

   //zw = zone->w;

   evas_object_smart_calculate(inst->o_box);
   EINA_LIST_FOREACH(inst->items, l, e)
   {
      if (e->isa_category)
        {
           if (!cath)
              edje_object_size_min_calc(e->o_holder, &catw, &cath);

           cat_count++;

           item_count = 0;
        } else
        {
           if (!ew && !eh)
              evas_object_geometry_get(e->o_holder, NULL, NULL, &ew, &eh);
           evas_object_resize(e->o_holder, ew, eh);

           if (max_item_count < ++item_count)
              max_item_count = item_count;
        }
   }
   if (!max_item_count) return;

   /* Calculate a symetric grid from max items count*/
   int row_item_count = ceil((float) sqrt(max_item_count));
   if (row_item_count > 5) row_item_count = 5; // max 5 rows grid
   cw = ew * row_item_count;
   ch = eh * ceil((float) max_item_count / row_item_count) + cath;

   /*
    * do
    * {
    *   cw = ew * row_item_count;
    *   ch = eh * ceil((float) max_item_count / row_item_count--) + cath;
    *   if (cat_count)
    *     ch *= cat_count;
    * } while (row_item_count && (cw > (zw - ew / 2) || ((double) cw / (double) ch) > 1.6));
    * catw = cw;
    */

   //~ iter_count = max_item_count;
   //~ while ((!hh || ((double) ww / (double) hh) < 1.5) && cw < (zw - ew / 2) && (ww < catw || (iter_count && ww <= max_item_count * ew + ew / 2)))
   //~ {
   //~ if (max_item_count == 1 && ww < catw)
   //~ cw = catw;
   //~ else
   //~ cw += ew;
   //~ ch += eh;
   //~ EINA_LIST_FOREACH(inst->items, l, e)
   //~ {
   //~ if (e->isa_category)
   //~ evas_object_resize(e->o_holder, cw, cath);
   //~ }
   //~ /* Rough approximation, since we don't know the box's
   //~ * padding settings, and we don't care */
   //~ evas_object_resize(inst->o_box, cw + ew / 2, ch + eh / 2);
   //~ evas_object_size_hint_min_get(edje_object_part_object_get(inst->o_box, "e.box.content"), &ww, &hh);
   //~ iter_count--;
   //~ };

   /* XXX: switch to size_min_calc when it starts working
    *
    * edje_object_size_min_calc(inst->o_box, &ww, &hh);
    *
    */
   //~ evas_object_size_hint_min_get(edje_object_part_object_get(inst->o_box, "e.box.content"), &ww, &hh);
   //~ evas_object_resize(inst->o_box, ww, hh);
   evas_object_resize(inst->o_box, cw + ew, ch + eh / 2);
   evas_object_size_hint_min_set(inst->o_scroll, cw, ch + eh / 2);
   edje_object_size_min_calc(inst->o_con, &w, &h);
   evas_object_size_hint_min_set(inst->o_con, w, h);
   evas_object_size_hint_min_set(inst->o_scroll, 0, 0);
}

static void
_grid_containers_create(Instance *inst)
{
   Evas *evas = inst->evas;
   inst->o_con = edje_object_add(evas);
   inst->o_box = edje_object_add(evas);

   if (!e_theme_edje_object_set(inst->o_con, "base/theme/modules/drawer", "modules/drawer/grid"))
      edje_object_file_set(inst->o_con, inst->theme_file, "modules/drawer/grid");
   if (!e_theme_edje_object_set(inst->o_box, "base/theme/modules/drawer", "modules/drawer/grid/box"))
      edje_object_file_set(inst->o_box, inst->theme_file, "modules/drawer/grid/box");

   evas_object_show(inst->o_box);
}

static Item *
_grid_item_create(Instance *inst, Drawer_Source_Item *si)
{
   Item *e = E_NEW(Item, 1);
   Evas_Coord w, h;

    e->o_holder = edje_object_add(inst->evas);
   if (!e_theme_edje_object_set(e->o_holder, "base/theme/modules/drawer",
                                "modules/drawer/grid/item"))
      edje_object_file_set(e->o_holder, inst->theme_file,
                           "modules/drawer/grid/item");

   edje_object_part_geometry_get(e->o_holder, "e.swallow.content", NULL, NULL, &w, &h);
   e->o_icon = drawer_util_icon_create(si, inst->evas, w, h);

   edje_object_part_swallow(e->o_holder, "e.swallow.content", e->o_icon);
   evas_object_pass_events_set(e->o_icon, 1);
   evas_object_show(e->o_icon);

   edje_object_part_text_set(e->o_holder, "e.text.label", si->label);

   evas_object_show(e->o_holder);

   /* XXX: remove this once evas_box is fixed */
   edje_object_size_min_calc(e->o_holder, &w, &h);
   evas_object_size_hint_min_set(e->o_holder, w, h);
   evas_object_resize(e->o_holder, w, h);

   e->inst = inst;
   e->si = si;
   e->isa_category = EINA_FALSE;

   edje_object_signal_callback_add(e->o_holder, "e,action,select", "drawer",
                                   _grid_entry_select_cb, e);
   edje_object_signal_callback_add(e->o_holder, "e,action,deselect", "drawer",
                                   _grid_entry_deselect_cb, e);
   edje_object_signal_callback_add(e->o_holder, "e,action,activate", "drawer",
                                   _grid_entry_activate_cb, e);
   edje_object_signal_callback_add(e->o_holder, "e,action,context", "drawer",
                                   _grid_entry_context_cb, e);

   return e;
}

static Item *
_grid_category_create(Instance *inst, Drawer_Source_Item *si)
{
   Item *e = E_NEW(Item, 1);
   char buf[1024];

   e->o_holder = edje_object_add(inst->evas);
   if (!e_theme_edje_object_set(e->o_holder, "base/theme/modules/drawer",
                                "modules/drawer/grid/category"))
      edje_object_file_set(e->o_holder, inst->theme_file,
                           "modules/drawer/grid/category");

   if (si->category)
      snprintf(buf, sizeof(buf), "%s", si->category);
   else
      snprintf(buf, sizeof(buf), "Uncategorised");

   edje_object_part_text_set(e->o_holder, "e.text.category", buf);

   e->inst = inst;
   e->si = si;
   e->isa_category = EINA_TRUE;

   evas_object_show(e->o_holder);

   return e;
}

static void _grid_items_free(Instance *inst)
{
   Item *e;

   EINA_LIST_FREE(inst->items, e)
     {
        if (e->o_icon) evas_object_del(e->o_icon);
        if (e->o_holder) evas_object_del(e->o_holder);
        E_FREE(e);
     }
}

static int
_grid_sort_by_category_cb(const void *d1, const void *d2)
{
   const Drawer_Source_Item *si1;
   const Drawer_Source_Item *si2;
   int ret;

   if (!(si1 = d1)) return -1;
   if (!(si2 = d2)) return 1;

   if (!si1->category) return -1;
   if (!si2->category) return 1;

   ret = strcmp(si1->category, si2->category);

   return ret > 0 ? 1 : -1;
}

static void
_grid_entry_select_cb(void *data, Evas_Object *obj __UNUSED__, const char *emission __UNUSED__,
                      const char *source __UNUSED__)
{
   Item *e = data;
   Instance *inst = e->inst;
   Drawer_Source_Item *si = e->si;

   edje_object_part_text_set(inst->o_con, "e.text.label", si->label);
   edje_object_part_text_set(inst->o_con, "e.text.description", si->description);
}

static void
_grid_entry_deselect_cb(void *data, Evas_Object *obj __UNUSED__, const char *emission __UNUSED__,
                        const char *source __UNUSED__)
{
   Item *e = data;
   Instance *inst = e->inst;

   edje_object_part_text_set(inst->o_con, "e.text.label", NULL);
   edje_object_part_text_set(inst->o_con, "e.text.description", NULL);
}

static void
_grid_entry_activate_cb(void *data, Evas_Object *obj __UNUSED__, const char *emission __UNUSED__,
                        const char *source __UNUSED__)
{
   Item *e = data;
   Drawer_Event_View_Activate *ev = E_NEW(Drawer_Event_View_Activate, 1);

   ev->data = e->si;
   ev->view = e->inst->view;
   ev->id = eina_stringshare_add(e->inst->parent_id);
   ecore_event_add(DRAWER_EVENT_VIEW_ITEM_ACTIVATE,
                   ev, _grid_event_activate_free, NULL);

   /* XXX: this doesn't seem to work */
   edje_object_signal_emit(e->inst->o_con, "e,action,activate", "drawer");
}

static void
_grid_entry_context_cb(void *data, Evas_Object *obj, const char *emission __UNUSED__, const char *source __UNUSED__)
{
   Item *e = data;
   Drawer_Event_View_Context *ev = E_NEW(Drawer_Event_View_Context, 1);
   Evas_Coord ox, oy;

   evas_object_geometry_get(obj, &ox, &oy, NULL, NULL);

   ev->data = e->si;
   ev->view = e->inst->view;
   ev->x = ox;
   ev->y = oy;
   ev->id = eina_stringshare_add(e->inst->parent_id);
   ecore_event_add(DRAWER_EVENT_VIEW_ITEM_CONTEXT,
                   ev, _grid_event_context_free, NULL);
}

static void
_grid_event_activate_free(void *data __UNUSED__, void *event)
{
   Drawer_Event_View_Activate *ev = event;

   eina_stringshare_del(ev->id);
   free(ev);
}

static void
_grid_event_context_free(void *data __UNUSED__, void *event)
{
   Drawer_Event_View_Context *ev = event;

   eina_stringshare_del(ev->id);
   free(ev);
}
