/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) Zend Technologies Ltd. (http://www.zend.com)           |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Marcus Boerger <helly@php.net>                              |
   +----------------------------------------------------------------------+
*/

#include "zend.h"
#include "zend_API.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_interfaces_arginfo.h"

ZEND_API zend_class_entry *zend_ce_traversable;
ZEND_API zend_class_entry *zend_ce_aggregate;
ZEND_API zend_class_entry *zend_ce_iterator;
ZEND_API zend_class_entry *zend_ce_arrayaccess;
ZEND_API zend_class_entry *zend_ce_serializable;
ZEND_API zend_class_entry *zend_ce_countable;
ZEND_API zend_class_entry *zend_ce_stringable;

/* {{{ zend_call_method
 Only returns the returned zval if retval_ptr != NULL */
ZEND_API zval* zend_call_method(zend_object *object, zend_class_entry *obj_ce, zend_function **fn_proxy, const char *function_name, size_t function_name_len, zval *retval_ptr, int param_count, zval* arg1, zval* arg2)
{
	int result;
	zend_fcall_info fci;
	zval retval;
	zval params[2];

	if (param_count > 0) {
		ZVAL_COPY_VALUE(&params[0], arg1);
	}
	if (param_count > 1) {
		ZVAL_COPY_VALUE(&params[1], arg2);
	}

	fci.size = sizeof(fci);
	fci.object = object;
	fci.retval = retval_ptr ? retval_ptr : &retval;
	fci.param_count = param_count;
	fci.params = params;
	fci.no_separation = 1;

	if (!fn_proxy && !obj_ce) {
		/* no interest in caching and no information already present that is
		 * needed later inside zend_call_function. */
		ZVAL_STRINGL(&fci.function_name, function_name, function_name_len);
		result = zend_call_function(&fci, NULL);
		zval_ptr_dtor(&fci.function_name);
	} else {
		zend_fcall_info_cache fcic;
		ZVAL_UNDEF(&fci.function_name); /* Unused */

		if (!obj_ce) {
			obj_ce = object ? object->ce : NULL;
		}
		if (!fn_proxy || !*fn_proxy) {
			if (EXPECTED(obj_ce)) {
				fcic.function_handler = zend_hash_str_find_ptr(
					&obj_ce->function_table, function_name, function_name_len);
				if (UNEXPECTED(fcic.function_handler == NULL)) {
					/* error at c-level */
					zend_error_noreturn(E_CORE_ERROR, "Couldn't find implementation for method %s::%s", ZSTR_VAL(obj_ce->name), function_name);
				}
			} else {
				fcic.function_handler = zend_fetch_function_str(function_name, function_name_len);
				if (UNEXPECTED(fcic.function_handler == NULL)) {
					/* error at c-level */
					zend_error_noreturn(E_CORE_ERROR, "Couldn't find implementation for function %s", function_name);
				}
			}
			if (fn_proxy) {
				*fn_proxy = fcic.function_handler;
			}
		} else {
			fcic.function_handler = *fn_proxy;
		}

		if (object) {
			fcic.called_scope = object->ce;
		} else {
			zend_class_entry *called_scope = zend_get_called_scope(EG(current_execute_data));

			if (obj_ce &&
			    (!called_scope ||
			     !instanceof_function(called_scope, obj_ce))) {
				fcic.called_scope = obj_ce;
			} else {
				fcic.called_scope = called_scope;
			}
		}
		fcic.object = object;
		result = zend_call_function(&fci, &fcic);
	}
	if (result == FAILURE) {
		/* error at c-level */
		if (!obj_ce) {
			obj_ce = object ? object->ce : NULL;
		}
		if (!EG(exception)) {
			zend_error_noreturn(E_CORE_ERROR, "Couldn't execute method %s%s%s", obj_ce ? ZSTR_VAL(obj_ce->name) : "", obj_ce ? "::" : "", function_name);
		}
	}
	if (!retval_ptr) {
		zval_ptr_dtor(&retval);
		return NULL;
	}
	return retval_ptr;
}
/* }}} */

/* iterator interface, c-level functions used by engine */

/* {{{ zend_user_it_new_iterator */
ZEND_API void zend_user_it_new_iterator(zend_class_entry *ce, zval *object, zval *retval)
{
	zend_call_method_with_0_params(Z_OBJ_P(object), ce, &ce->iterator_funcs_ptr->zf_new_iterator, "getiterator", retval);
}
/* }}} */

/* {{{ zend_user_it_invalidate_current */
ZEND_API void zend_user_it_invalidate_current(zend_object_iterator *_iter)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;

	if (!Z_ISUNDEF(iter->value)) {
		zval_ptr_dtor(&iter->value);
		ZVAL_UNDEF(&iter->value);
	}
}
/* }}} */

/* {{{ zend_user_it_dtor */
static void zend_user_it_dtor(zend_object_iterator *_iter)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;
	zval *object = &iter->it.data;

	zend_user_it_invalidate_current(_iter);
	zval_ptr_dtor(object);
}
/* }}} */

/* {{{ zend_user_it_valid */
ZEND_API int zend_user_it_valid(zend_object_iterator *_iter)
{
	if (_iter) {
		zend_user_iterator *iter = (zend_user_iterator*)_iter;
		zval *object = &iter->it.data;
		zval more;
		int result;

		zend_call_method_with_0_params(Z_OBJ_P(object), iter->ce, &iter->ce->iterator_funcs_ptr->zf_valid, "valid", &more);
		result = i_zend_is_true(&more);
		zval_ptr_dtor(&more);
		return result ? SUCCESS : FAILURE;
	}
	return FAILURE;
}
/* }}} */

/* {{{ zend_user_it_get_current_data */
ZEND_API zval *zend_user_it_get_current_data(zend_object_iterator *_iter)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;
	zval *object = &iter->it.data;

	if (Z_ISUNDEF(iter->value)) {
		zend_call_method_with_0_params(Z_OBJ_P(object), iter->ce, &iter->ce->iterator_funcs_ptr->zf_current, "current", &iter->value);
	}
	return &iter->value;
}
/* }}} */

/* {{{ zend_user_it_get_current_key */
ZEND_API void zend_user_it_get_current_key(zend_object_iterator *_iter, zval *key)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;
	zval *object = &iter->it.data;
	zval retval;

	zend_call_method_with_0_params(Z_OBJ_P(object), iter->ce, &iter->ce->iterator_funcs_ptr->zf_key, "key", &retval);

	if (Z_TYPE(retval) != IS_UNDEF) {
		ZVAL_COPY_VALUE(key, &retval);
	} else {
		if (!EG(exception)) {
			zend_error(E_WARNING, "Nothing returned from %s::key()", ZSTR_VAL(iter->ce->name));
		}

		ZVAL_LONG(key, 0);
	}
}
/* }}} */

/* {{{ zend_user_it_move_forward */
ZEND_API void zend_user_it_move_forward(zend_object_iterator *_iter)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;
	zval *object = &iter->it.data;

	zend_user_it_invalidate_current(_iter);
	zend_call_method_with_0_params(Z_OBJ_P(object), iter->ce, &iter->ce->iterator_funcs_ptr->zf_next, "next", NULL);
}
/* }}} */

/* {{{ zend_user_it_rewind */
ZEND_API void zend_user_it_rewind(zend_object_iterator *_iter)
{
	zend_user_iterator *iter = (zend_user_iterator*)_iter;
	zval *object = &iter->it.data;

	zend_user_it_invalidate_current(_iter);
	zend_call_method_with_0_params(Z_OBJ_P(object), iter->ce, &iter->ce->iterator_funcs_ptr->zf_rewind, "rewind", NULL);
}
/* }}} */

static const zend_object_iterator_funcs zend_interface_iterator_funcs_iterator = {
	zend_user_it_dtor,
	zend_user_it_valid,
	zend_user_it_get_current_data,
	zend_user_it_get_current_key,
	zend_user_it_move_forward,
	zend_user_it_rewind,
	zend_user_it_invalidate_current
};

/* {{{ zend_user_it_get_iterator */
static zend_object_iterator *zend_user_it_get_iterator(zend_class_entry *ce, zval *object, int by_ref)
{
	zend_user_iterator *iterator;

	if (by_ref) {
		zend_throw_error(NULL, "An iterator cannot be used with foreach by reference");
		return NULL;
	}

	iterator = emalloc(sizeof(zend_user_iterator));

	zend_iterator_init((zend_object_iterator*)iterator);

	Z_ADDREF_P(object);
	ZVAL_OBJ(&iterator->it.data, Z_OBJ_P(object));
	iterator->it.funcs = &zend_interface_iterator_funcs_iterator;
	iterator->ce = Z_OBJCE_P(object);
	ZVAL_UNDEF(&iterator->value);
	return (zend_object_iterator*)iterator;
}
/* }}} */

/* {{{ zend_user_it_get_new_iterator */
ZEND_API zend_object_iterator *zend_user_it_get_new_iterator(zend_class_entry *ce, zval *object, int by_ref)
{
	zval iterator;
	zend_object_iterator *new_iterator;
	zend_class_entry *ce_it;

	zend_user_it_new_iterator(ce, object, &iterator);
	ce_it = (Z_TYPE(iterator) == IS_OBJECT) ? Z_OBJCE(iterator) : NULL;

	if (!ce_it || !ce_it->get_iterator || (ce_it->get_iterator == zend_user_it_get_new_iterator && Z_OBJ(iterator) == Z_OBJ_P(object))) {
		if (!EG(exception)) {
			zend_throw_exception_ex(NULL, 0, "Objects returned by %s::getIterator() must be traversable or implement interface Iterator", ce ? ZSTR_VAL(ce->name) : ZSTR_VAL(Z_OBJCE_P(object)->name));
		}
		zval_ptr_dtor(&iterator);
		return NULL;
	}

	new_iterator = ce_it->get_iterator(ce_it, &iterator, by_ref);
	zval_ptr_dtor(&iterator);
	return new_iterator;
}
/* }}} */

/* {{{ zend_implement_traversable */
static int zend_implement_traversable(zend_class_entry *interface, zend_class_entry *class_type)
{
	/* check that class_type is traversable at c-level or implements at least one of 'aggregate' and 'Iterator' */
	uint32_t i;

	if (class_type->get_iterator || (class_type->parent && class_type->parent->get_iterator)) {
		return SUCCESS;
	}
	if (class_type->num_interfaces) {
		ZEND_ASSERT(class_type->ce_flags & ZEND_ACC_RESOLVED_INTERFACES);
		for (i = 0; i < class_type->num_interfaces; i++) {
			if (class_type->interfaces[i] == zend_ce_aggregate || class_type->interfaces[i] == zend_ce_iterator) {
				return SUCCESS;
			}
		}
	}
	zend_error_noreturn(E_CORE_ERROR, "Class %s must implement interface %s as part of either %s or %s",
		ZSTR_VAL(class_type->name),
		ZSTR_VAL(zend_ce_traversable->name),
		ZSTR_VAL(zend_ce_iterator->name),
		ZSTR_VAL(zend_ce_aggregate->name));
	return FAILURE;
}
/* }}} */

/* {{{ zend_implement_aggregate */
static int zend_implement_aggregate(zend_class_entry *interface, zend_class_entry *class_type)
{
	uint32_t i;
	int t = -1;
	zend_class_iterator_funcs *funcs_ptr;

	if (class_type->get_iterator) {
		if (class_type->type == ZEND_INTERNAL_CLASS) {
			/* inheritance ensures the class has necessary userland methods */
			return SUCCESS;
		} else if (class_type->get_iterator != zend_user_it_get_new_iterator) {
			/* c-level get_iterator cannot be changed (exception being only Traversable is implemented) */
			if (class_type->num_interfaces) {
				ZEND_ASSERT(class_type->ce_flags & ZEND_ACC_RESOLVED_INTERFACES);
				for (i = 0; i < class_type->num_interfaces; i++) {
					if (class_type->interfaces[i] == zend_ce_iterator) {
						zend_error_noreturn(E_ERROR, "Class %s cannot implement both %s and %s at the same time",
									ZSTR_VAL(class_type->name),
									ZSTR_VAL(interface->name),
									ZSTR_VAL(zend_ce_iterator->name));
						return FAILURE;
					}
					if (class_type->interfaces[i] == zend_ce_traversable) {
						t = i;
					}
				}
			}
			if (t == -1) {
				return FAILURE;
			}
		}
	}
	if (class_type->parent
	 && (class_type->parent->ce_flags & ZEND_ACC_REUSE_GET_ITERATOR)) {
		class_type->get_iterator = class_type->parent->get_iterator;
		class_type->ce_flags |= ZEND_ACC_REUSE_GET_ITERATOR;
	} else {
		class_type->get_iterator = zend_user_it_get_new_iterator;
	}
	funcs_ptr = class_type->iterator_funcs_ptr;
	if (class_type->type == ZEND_INTERNAL_CLASS) {
		if (!funcs_ptr) {
			funcs_ptr = calloc(1, sizeof(zend_class_iterator_funcs));
			class_type->iterator_funcs_ptr = funcs_ptr;
		}
		funcs_ptr->zf_new_iterator = zend_hash_str_find_ptr(&class_type->function_table, "getiterator", sizeof("getiterator") - 1);
	} else {
		if (!funcs_ptr) {
			funcs_ptr = zend_arena_alloc(&CG(arena), sizeof(zend_class_iterator_funcs));
			class_type->iterator_funcs_ptr = funcs_ptr;
			memset(funcs_ptr, 0, sizeof(zend_class_iterator_funcs));
		} else {
			funcs_ptr->zf_new_iterator = NULL;
		}
	}
	return SUCCESS;
}
/* }}} */

/* {{{ zend_implement_iterator */
static int zend_implement_iterator(zend_class_entry *interface, zend_class_entry *class_type)
{
	zend_class_iterator_funcs *funcs_ptr;

	if (class_type->get_iterator && class_type->get_iterator != zend_user_it_get_iterator) {
		if (class_type->type == ZEND_INTERNAL_CLASS) {
			/* inheritance ensures the class has the necessary userland methods */
			return SUCCESS;
		} else {
			/* c-level get_iterator cannot be changed */
			if (class_type->get_iterator == zend_user_it_get_new_iterator) {
				zend_error_noreturn(E_ERROR, "Class %s cannot implement both %s and %s at the same time",
							ZSTR_VAL(class_type->name),
							ZSTR_VAL(interface->name),
							ZSTR_VAL(zend_ce_aggregate->name));
			}
			return FAILURE;
		}
	}
	if (class_type->parent
	 && (class_type->parent->ce_flags & ZEND_ACC_REUSE_GET_ITERATOR)) {
		class_type->get_iterator = class_type->parent->get_iterator;
		class_type->ce_flags |= ZEND_ACC_REUSE_GET_ITERATOR;
	} else {
		class_type->get_iterator = zend_user_it_get_iterator;
	}
	funcs_ptr = class_type->iterator_funcs_ptr;
	if (class_type->type == ZEND_INTERNAL_CLASS) {
		if (!funcs_ptr) {
			funcs_ptr = calloc(1, sizeof(zend_class_iterator_funcs));
			class_type->iterator_funcs_ptr = funcs_ptr;
		} else {
			funcs_ptr->zf_rewind = zend_hash_str_find_ptr(&class_type->function_table, "rewind", sizeof("rewind") - 1);
			funcs_ptr->zf_valid = zend_hash_str_find_ptr(&class_type->function_table, "valid", sizeof("valid") - 1);
			funcs_ptr->zf_key = zend_hash_str_find_ptr(&class_type->function_table, "key", sizeof("key") - 1);
			funcs_ptr->zf_current = zend_hash_str_find_ptr(&class_type->function_table, "current", sizeof("current") - 1);
			funcs_ptr->zf_next = zend_hash_str_find_ptr(&class_type->function_table, "next", sizeof("next") - 1);
		}
	} else {
		if (!funcs_ptr) {
			funcs_ptr = zend_arena_alloc(&CG(arena), sizeof(zend_class_iterator_funcs));
			class_type->iterator_funcs_ptr = funcs_ptr;
			memset(funcs_ptr, 0, sizeof(zend_class_iterator_funcs));
		} else {
			funcs_ptr->zf_valid = NULL;
			funcs_ptr->zf_current = NULL;
			funcs_ptr->zf_key = NULL;
			funcs_ptr->zf_next = NULL;
			funcs_ptr->zf_rewind = NULL;
		}
	}
	return SUCCESS;
}
/* }}} */

/* {{{ zend_user_serialize */
ZEND_API int zend_user_serialize(zval *object, unsigned char **buffer, size_t *buf_len, zend_serialize_data *data)
{
	zend_class_entry * ce = Z_OBJCE_P(object);
	zval retval;
	int result;

	zend_call_method_with_0_params(Z_OBJ_P(object), ce, &ce->serialize_func, "serialize", &retval);


	if (Z_TYPE(retval) == IS_UNDEF || EG(exception)) {
		result = FAILURE;
	} else {
		switch(Z_TYPE(retval)) {
		case IS_NULL:
			/* we could also make this '*buf_len = 0' but this allows to skip variables */
			zval_ptr_dtor(&retval);
			return FAILURE;
		case IS_STRING:
			*buffer = (unsigned char*)estrndup(Z_STRVAL(retval), Z_STRLEN(retval));
			*buf_len = Z_STRLEN(retval);
			result = SUCCESS;
			break;
		default: /* failure */
			result = FAILURE;
			break;
		}
		zval_ptr_dtor(&retval);
	}

	if (result == FAILURE && !EG(exception)) {
		zend_throw_exception_ex(NULL, 0, "%s::serialize() must return a string or NULL", ZSTR_VAL(ce->name));
	}
	return result;
}
/* }}} */

/* {{{ zend_user_unserialize */
ZEND_API int zend_user_unserialize(zval *object, zend_class_entry *ce, const unsigned char *buf, size_t buf_len, zend_unserialize_data *data)
{
	zval zdata;

	if (UNEXPECTED(object_init_ex(object, ce) != SUCCESS)) {
		return FAILURE;
	}

	ZVAL_STRINGL(&zdata, (char*)buf, buf_len);

	zend_call_method_with_1_params(Z_OBJ_P(object), ce, &ce->unserialize_func, "unserialize", NULL, &zdata);

	zval_ptr_dtor(&zdata);

	if (EG(exception)) {
		return FAILURE;
	} else {
		return SUCCESS;
	}
}
/* }}} */

ZEND_API int zend_class_serialize_deny(zval *object, unsigned char **buffer, size_t *buf_len, zend_serialize_data *data) /* {{{ */
{
	zend_class_entry *ce = Z_OBJCE_P(object);
	zend_throw_exception_ex(NULL, 0, "Serialization of '%s' is not allowed", ZSTR_VAL(ce->name));
	return FAILURE;
}
/* }}} */

ZEND_API int zend_class_unserialize_deny(zval *object, zend_class_entry *ce, const unsigned char *buf, size_t buf_len, zend_unserialize_data *data) /* {{{ */
{
	zend_throw_exception_ex(NULL, 0, "Unserialization of '%s' is not allowed", ZSTR_VAL(ce->name));
	return FAILURE;
}
/* }}} */

/* {{{ zend_implement_serializable */
static int zend_implement_serializable(zend_class_entry *interface, zend_class_entry *class_type)
{
	if (class_type->parent
		&& (class_type->parent->serialize || class_type->parent->unserialize)
		&& !zend_class_implements_interface(class_type->parent, zend_ce_serializable)) {
		return FAILURE;
	}
	if (!class_type->serialize) {
		class_type->serialize = zend_user_serialize;
	}
	if (!class_type->unserialize) {
		class_type->unserialize = zend_user_unserialize;
	}
	return SUCCESS;
}
/* }}}*/

/* {{{ function tables */
static const zend_function_entry zend_funcs_aggregate[] = {
	ZEND_ABSTRACT_ME(iterator, getIterator, arginfo_class_IteratorAggregate_getIterator)
	ZEND_FE_END
};

static const zend_function_entry zend_funcs_iterator[] = {
	ZEND_ABSTRACT_ME(iterator, current,  arginfo_class_Iterator_current)
	ZEND_ABSTRACT_ME(iterator, next,     arginfo_class_Iterator_next)
	ZEND_ABSTRACT_ME(iterator, key,      arginfo_class_Iterator_key)
	ZEND_ABSTRACT_ME(iterator, valid,    arginfo_class_Iterator_valid)
	ZEND_ABSTRACT_ME(iterator, rewind,   arginfo_class_Iterator_rewind)
	ZEND_FE_END
};

static const zend_function_entry *zend_funcs_traversable = NULL;

static const zend_function_entry zend_funcs_arrayaccess[] = {
	ZEND_ABSTRACT_ME(arrayaccess, offsetExists, arginfo_class_ArrayAccess_offsetExists)
	ZEND_ABSTRACT_ME(arrayaccess, offsetGet,    arginfo_class_ArrayAccess_offsetGet)
	ZEND_ABSTRACT_ME(arrayaccess, offsetSet,    arginfo_class_ArrayAccess_offsetSet)
	ZEND_ABSTRACT_ME(arrayaccess, offsetUnset,  arginfo_class_ArrayAccess_offsetUnset)
	ZEND_FE_END
};

static const zend_function_entry zend_funcs_serializable[] = {
	ZEND_ABSTRACT_ME(serializable, serialize, arginfo_class_Serializable_serialize)
	ZEND_FENTRY(unserialize, NULL, arginfo_class_Serializable_unserialize, ZEND_ACC_PUBLIC|ZEND_ACC_ABSTRACT)
	ZEND_FE_END
};

static const zend_function_entry zend_funcs_countable[] = {
	ZEND_ABSTRACT_ME(Countable, count, arginfo_class_Countable_count)
	ZEND_FE_END
};

static const zend_function_entry zend_funcs_stringable[] = {
	ZEND_ABSTRACT_ME(Stringable, __toString, arginfo_class_Stringable___toString)
	ZEND_FE_END
};
/* }}} */

/* {{{ zend_register_interfaces */
ZEND_API void zend_register_interfaces(void)
{
	REGISTER_MAGIC_INTERFACE(traversable, Traversable);

	REGISTER_MAGIC_INTERFACE(aggregate, IteratorAggregate);
	REGISTER_MAGIC_IMPLEMENT(aggregate, traversable);

	REGISTER_MAGIC_INTERFACE(iterator, Iterator);
	REGISTER_MAGIC_IMPLEMENT(iterator, traversable);

	REGISTER_MAGIC_INTERFACE(serializable, Serializable);

	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "ArrayAccess", zend_funcs_arrayaccess);
	zend_ce_arrayaccess = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Countable", zend_funcs_countable);
	zend_ce_countable = zend_register_internal_interface(&ce);

	INIT_CLASS_ENTRY(ce, "Stringable", zend_funcs_stringable);
	zend_ce_stringable = zend_register_internal_interface(&ce);
}
/* }}} */
