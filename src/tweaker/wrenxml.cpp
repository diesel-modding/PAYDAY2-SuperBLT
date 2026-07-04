#include "wrenxml.h"

#include "global.h"

#include <string>
#include <util/util.h>

using namespace std;
using namespace raidhook::tweaker;
using namespace wrenxml;

const char* MODULE = "base/native";

#define WXML_ERR(err)                                                                                         \
	{                                                                                                         \
		RAIDHOOK_LOG_ERROR(err);                                                                              \
		string str = err;                                                                                     \
		wrenSetSlotString(vm, 0, str.c_str());                                                                \
		wrenAbortFiber(vm, 0);                                                                                \
		/*MessageBox(0, "A WXML Error has occured - details are logged in the console", "WXML Error", MB_OK); \
		exit(1);*/                                                                                            \
	}

#define GET_WXML_NODE(vm, slot, name)                           \
	WXMLNode* name = *(WXMLNode**)wrenGetSlotForeign(vm, slot); \
	if (name->root == NULL)                                     \
	{                                                           \
		WXML_ERR("Cannot use closed Wren XML Instance");        \
	}

#define THIS_WXML_NODE(vm)              \
	GET_WXML_NODE(vm, 0, wxml)          \
	mxml_node_t* handle = wxml->handle; \
	(void)(handle) /* cast our handle to void to eliminate GCC's unused variable errors */

static char* /* O - Allocated string */
mxmlToAllocStringSafe(mxml_node_t* node, /* I - Node to write */
                      mxml_save_cb_t cb) /* I - Whitespace callback or @code MXML_NO_CALLBACK@ */
{
	int bytes; /* Required bytes */
	char buffer[8192]; /* Temporary buffer */
	char* s; /* Allocated string */

	/*
	 * Write the node to the temporary buffer...
	 */

	bytes = mxmlSaveString(node, buffer, sizeof(buffer), cb);

	if (bytes <= 0)
		return strdup(""); // Empty string case

	if (bytes < (int)(sizeof(buffer) - 1))
	{
		/*
		 * Node fit inside the buffer, so just duplicate that string and
		 * return...
		 */

		return strdup(buffer);
	}

	/*
	 * Allocate a buffer of the required size and save the node to the
	 * new buffer...
	 */

	if ((s = (char*)malloc(bytes + 1)) == NULL)
	{
		const char* message = "[WREN/WXML] XML.string: Cannot allocate enough space for XML string";
		RAIDHOOK_LOG_ERROR(message);
		MessageBox(0, message, "SuperBLT OutOfMemory", MB_OK);
		exit(1);
	}

	mxmlSaveString(node, s, bytes + 1, cb);

	/*
	 * Return the allocated string...
	 */

	return (s);
}

static const char* last_loaded_xml = NULL;
static char* mxml_last_error = NULL;

static void handle_mxml_error_crash(const char* error)
{
	RAIDHOOK_LOG_ERROR("Could not parse XML: Error and original file below");
	RAIDHOOK_LOG_ERROR(error);
	RAIDHOOK_LOG_ERROR(last_loaded_xml);

	const char* message = "[WREN/WXML] XML parse error - see mods/logs for details";
	MessageBox(0, message, "XML Parse Error", MB_OK);
	exit(1);
}

static void handle_mxml_error_note(const char* error)
{
	mxml_last_error = strdup(error);
}

static mxml_node_t* recursive_clone(mxml_node_t* dest_parent, mxml_node_t* src)
{
	mxml_node_t* dest = mxmlNewElement(dest_parent, mxmlGetElement(src));
	for (int i = 0; i < mxmlElementGetAttrCount(src); i++)
	{
		const char* name;
		const char* value = mxmlElementGetAttrByIndex(src, i, &name);
		mxmlElementSetAttr(dest, name, value);
	}

	mxml_node_t* src_child = mxmlGetFirstChild(src);
	while (src_child != NULL)
	{
		recursive_clone(dest, src_child);

		src_child = mxmlGetNextSibling(src_child);
	}

	return dest;
}

mxml_type_t remove_whitespace_callback(mxml_node_t*)
{
	return MXML_IGNORE;
}

WXMLDocument::WXMLDocument(const char* text)
{
	root_node = mxmlLoadString(NULL, text, remove_whitespace_callback);
}

WXMLDocument::WXMLDocument(WXMLNode* clone_from)
{
	root_node = recursive_clone(MXML_NO_PARENT, clone_from->handle);
}

WXMLDocument::WXMLDocument(mxml_node_t* root_node) : root_node(root_node)
{
}

WXMLDocument::~WXMLDocument()
{
	for (auto const& node : nodes)
	{
		node.second->root = NULL;
		node.second->handle = NULL;
	}

	mxmlDelete(root_node);
}

WXMLNode* WXMLDocument::GetNode(mxml_node_t* node)
{
	if (nodes.count(node))
		return nodes[node];

	return new WXMLNode(this, node);
}

void WXMLDocument::MergeInto(WXMLDocument* other)
{
	for (auto const& pair : nodes)
	{
		other->nodes[pair.first] = pair.second;
		pair.second->root = other;
	}
	nodes.clear();
	root_node = NULL;
	// TODO mark ourselves for deletion
}

WXMLNode::WXMLNode(WXMLDocument* root, mxml_node_t* handle) : root(root), handle(handle), usages(0)
{
	root->nodes[handle] = this;
}

void WXMLNode::Release()
{
	usages--;
	if (usages <= 0)
	{
		if (root != NULL)
		{
			root->nodes.erase(handle);

			if (root->nodes.empty())
			{
				delete root;
			}
		}

		handle = NULL;
		root = NULL;
		delete this; // DANGER!
	}
}

WXMLDocument* WXMLNode::MoveToNewDocument()
{
	WXMLDocument* old = root;
	WXMLDocument* doc = new WXMLDocument(handle);

	vector<mxml_node_t*> to_remove;
	for (auto const& node : old->nodes)
	{
		mxml_node_t* nod = node.first;
		bool is_child = false;
		while (nod != NULL)
		{
			if (nod == handle)
			{
				is_child = true;
				break;
			}
			nod = mxmlGetParent(nod);
		}

		if (!is_child)
			continue;

		to_remove.push_back(node.first);
		node.second->root = doc;
		doc->nodes[node.first] = node.second;
	}

	for (mxml_node_t* const& node : to_remove)
	{
		old->nodes.erase(node);
	}

	mxmlRemove(handle);

	return doc;
}

static WXMLNode* attemptParseString(WrenVM* vm)
{
	WXMLNode** node = (WXMLNode**)wrenSetSlotNewForeign(vm, 0, 0, sizeof(WXMLNode*));

	const char* text = wrenGetSlotString(vm, 1);
	last_loaded_xml = text;

	WXMLDocument* doc = new WXMLDocument(text);

	*node = doc->GetRootNode();
	(*node)->Use();

	// Use the crash callback for anything else
	mxmlSetErrorCallback(handle_mxml_error_crash);

	return *node;
}

static void allocateXML(WrenVM* vm)
{
	mxmlSetErrorCallback(handle_mxml_error_crash);
	WXMLNode* wxml = attemptParseString(vm);

	if (!wxml->handle)
	{
		WXML_ERR("Uncaught parse error!");
	}
}

static void finalizeXML(void* data)
{
	WXMLNode* wxml = *(WXMLNode**)data;
	wxml->Release();
}

static void XMLtry_parse(WrenVM* vm)
{
	mxmlSetErrorCallback(handle_mxml_error_note);
	WXMLNode* wxml = attemptParseString(vm);

	if (mxml_last_error)
	{
		finalizeXML(wxml);
		wrenSetSlotNull(vm, 0);

		free(mxml_last_error);
		mxml_last_error = NULL;
	}
}

static void XMLNode_delete(WrenVM* vm)
{
	// Make sure we don't crash if already freed, so don't use THIS_WXML
	WXMLNode* wxml = *(WXMLNode**)wrenGetSlotForeign(vm, 0);
	if (wxml->root != NULL)
		delete wxml->root;
}

static WXMLNode* XMLNode_create(WrenVM* vm, WXMLDocument* root, mxml_node_t* xnode, int slot)
{
	if (xnode == NULL)
		WXML_ERR("Cannot create null XML Node");

	wrenGetVariable(vm, MODULE, "XML", slot);

	WXMLNode** node = (WXMLNode**)wrenSetSlotNewForeign(vm, slot, slot, sizeof(WXMLNode*));
	*node = root->GetNode(xnode);
	(*node)->Use();

	return *node;
}

static void XMLNode_type(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	wrenSetSlotDouble(vm, 0, mxmlGetType(handle));
}

#define XMLNODE_REQUIRE_TYPE(type, name) \
	if (mxmlGetType(handle) != type)     \
		WXML_ERR("Can only perform ." #name " on " #type " nodes - ID:" + to_string(mxmlGetType(handle)));

static void XMLNode_text(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_TEXT, name);

	wrenSetSlotString(vm, 0, mxmlGetText(handle, NULL));
}

static void XMLNode_text_set(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_TEXT, name);

	mxmlSetText(handle, 0, wrenGetSlotString(vm, 1));
}

static void XMLNode_string(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, string);

	mxmlSetWrapMargin(0);
	char* str = mxmlToAllocStringSafe(handle, MXML_NO_CALLBACK);
	wrenSetSlotString(vm, 0, str);
	free(str);
}

static void XMLNode_name(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	wrenSetSlotString(vm, 0, mxmlGetElement(handle));
}

static void XMLNode_name_set(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	mxmlSetElement(handle, wrenGetSlotString(vm, 1));
}

static void XMLNode_attribute(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	const char* value = mxmlElementGetAttr(handle, wrenGetSlotString(vm, 1));
	if (value)
		wrenSetSlotString(vm, 0, value);
	else
		wrenSetSlotNull(vm, 0);
}

static void XMLNode_attribute_set(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	const char* name = wrenGetSlotString(vm, 1);

	if (wrenGetSlotType(vm, 2) == WREN_TYPE_NULL)
	{
		mxmlElementDeleteAttr(handle, name);
	}
	else
	{
		const char* value = wrenGetSlotString(vm, 2);
		mxmlElementSetAttr(handle, name, value);
	}
}

static void XMLNode_attribute_names(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	wrenEnsureSlots(vm, 2);

	wrenSetSlotNewList(vm, 0);
	for (int i = 0; i < mxmlElementGetAttrCount(handle); i++)
	{
		const char* name;
		mxmlElementGetAttrByIndex(handle, i, &name);
		wrenSetSlotString(vm, 1, name);
		wrenInsertInList(vm, 0, -1, 1);
	}
}

static void XMLNode_create_element(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	XMLNODE_REQUIRE_TYPE(MXML_ELEMENT, name);

	const char* name = wrenGetSlotString(vm, 1);
	mxml_node_t* node = mxmlNewElement(handle, name);
	XMLNode_create(vm, wxml->root, node, 0);
}

static void XMLNode_detach(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	// Don't do anything if we're already at the top of a tree
	if (mxmlGetParent(handle) == NULL)
		return;

	wxml->MoveToNewDocument();
}

static void XMLNode_clone(WrenVM* vm)
{
	THIS_WXML_NODE(vm);

	WXMLDocument* doc = new WXMLDocument(wxml);
	XMLNode_create(vm, doc, doc->GetRootNode()->handle, 0);
}

static void XMLNode_attach(WrenVM* vm, int where, mxml_node_t* child)
{
	THIS_WXML_NODE(vm);
	GET_WXML_NODE(vm, 1, new_child);

	if (mxmlGetParent(new_child->handle) != NULL)
	{
		WXML_ERR("Cannot attach a node that already has a parent");
	}

	WXMLDocument* old_root = new_child->root;

	mxmlAdd(handle, where, child, new_child->handle);
	new_child->root->MergeInto(wxml->root);
	delete old_root;
}

static void XMLNode_attach(WrenVM* vm)
{
	XMLNode_attach(vm, MXML_ADD_AFTER, MXML_ADD_TO_PARENT);
}

static void XMLNode_attach_pos(WrenVM* vm)
{
	THIS_WXML_NODE(vm);
	GET_WXML_NODE(vm, 1, new_child);

	if (wrenGetSlotType(vm, 2) == WREN_TYPE_NULL)
	{
		XMLNode_attach(vm, MXML_ADD_BEFORE, MXML_ADD_TO_PARENT);
	}
	else
	{
		GET_WXML_NODE(vm, 2, prev_child);

		XMLNode_attach(vm, MXML_ADD_AFTER, prev_child->handle);
	}
}

#define XMLNODE_ACTION_FUNC(getter, name)            \
	static void XMLNode_##name(WrenVM* vm)           \
	{                                                \
		THIS_WXML_NODE(vm);                          \
		mxml_node_t* node = mxmlGet##getter(handle); \
		if (node)                                    \
		{                                            \
			XMLNode_create(vm, wxml->root, node, 0); \
		}                                            \
		else                                         \
		{                                            \
			wrenSetSlotNull(vm, 0);                  \
		}                                            \
	}

#define XMLNODE_FUNC_SET(func)                                                                         \
	func(NextSibling, next) func(PrevSibling, prev) func(Parent, parent) func(FirstChild, first_child) \
		func(LastChild, last_child)

XMLNODE_FUNC_SET(XMLNODE_ACTION_FUNC)

WrenForeignMethodFn wrenxml::bind_wxml_method(WrenVM* vm, const char* module, const char* class_name_s, bool is_static,
                                              const char* signature_c)
{
	if (strcmp(module, MODULE) != 0)
		return NULL;

	string class_name = class_name_s;
	string signature = signature_c;

	if (class_name == "XML")
	{
		if (is_static && signature == "try_parse(_)")
		{
			return XMLtry_parse;
		}

#define XMLNODE_DIFF_FUNC(name, sig)    \
	if (!is_static && signature == sig) \
	{                                   \
		return XMLNode_##name;          \
	}

#define XMLNODE_FUNC(name, sig) XMLNODE_DIFF_FUNC(name, #name sig)
#define XMLNODE_FUNC_FLAT(name) XMLNODE_DIFF_FUNC(name, #name)

#define XMLNODE_CHECK_FUNC(getter, name) XMLNODE_FUNC_FLAT(name)

#define XMLNODE_BI_FUNC(name)                    \
	XMLNODE_FUNC_FLAT(name)                      \
	if (!is_static && signature == #name "=(_)") \
	{                                            \
		return XMLNode_##name##_set;             \
	}

		XMLNODE_FUNC_FLAT(type);
		XMLNODE_FUNC_FLAT(string);
		XMLNODE_FUNC_FLAT(attribute_names);

		XMLNODE_BI_FUNC(text);
		XMLNODE_BI_FUNC(name);

		XMLNODE_FUNC(create_element, "(_)");

		XMLNODE_FUNC(detach, "()");
		XMLNODE_FUNC(clone, "()");
		XMLNODE_FUNC(attach, "(_)");
		XMLNODE_DIFF_FUNC(attach_pos, "attach(_,_)");

		XMLNODE_FUNC(delete, "()");

		XMLNODE_DIFF_FUNC(attribute, "[_]");
		XMLNODE_DIFF_FUNC(attribute_set, "[_]=(_)");

		XMLNODE_FUNC_SET(XMLNODE_CHECK_FUNC);
	}

	return NULL;
}

WrenForeignClassMethods wrenxml::get_XML_class_def(WrenVM* vm, const char* module, const char* class_name)
{
	if (string(module) == "base/native")
	{
		if (string(class_name) == "XML")
		{
			WrenForeignClassMethods def;
			def.allocate = allocateXML;
			def.finalize = finalizeXML;
			return def;
		}
	}
	return {NULL, NULL};
}
