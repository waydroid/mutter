
#ifndef ___mutter_marshal_MARSHAL_H__
#define ___mutter_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:INT,INT,ENUM (./mutter-marshal.list:1) */
extern void _mutter_marshal_VOID__INT_INT_ENUM (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:INT,OBJECT (./mutter-marshal.list:2) */
extern void _mutter_marshal_VOID__INT_OBJECT (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

G_END_DECLS

#endif /* ___mutter_marshal_MARSHAL_H__ */

