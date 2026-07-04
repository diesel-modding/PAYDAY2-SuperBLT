#pragma once

#include <map>
#include <unordered_set>

#include "mxml.h"

#include <wren.hpp>

namespace raidhook
{
	namespace tweaker
	{
		namespace wrenxml
		{
			class WXMLNode;

			class WXMLDocument
			{
			  public:
				WXMLDocument(const char* text);
				WXMLDocument(WXMLNode* clone_from);
				~WXMLDocument();
				WXMLNode* GetRootNode()
				{
					return GetNode(root_node);
				}
				WXMLNode* GetNode(mxml_node_t* node);
				void MergeInto(WXMLDocument* other);

			  private:
				mxml_node_t* root_node;
				std::map<mxml_node_t*, WXMLNode*> nodes;

				WXMLDocument(mxml_node_t* root_node);

				friend class WXMLNode;
			};

			// Note that having nodes open does not prevent the main object from being GC'd
			class WXMLNode
			{
			  public:
				WXMLDocument* root;
				mxml_node_t* handle;
				void Use()
				{
					usages++;
				}
				void Release();
				WXMLDocument* MoveToNewDocument();

			  private:
				WXMLNode(WXMLDocument* root, mxml_node_t* handle);

				int usages;

				friend class WXMLDocument;
			};

			WrenForeignMethodFn bind_wxml_method(WrenVM* vm, const char* module, const char* className, bool isStatic,
			                                     const char* signature);

			WrenForeignClassMethods get_XML_class_def(WrenVM* vm, const char* module, const char* class_name);
		}; // namespace wrenxml
	}; // namespace tweaker
}; // namespace raidhook
