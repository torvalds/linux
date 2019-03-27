/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 David Chisnall
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _FDT_HH_
#define _FDT_HH_
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <functional>

#include "util.hh"
#include "input_buffer.hh"

namespace dtc
{

namespace dtb 
{
struct output_writer;
class string_table;
}

namespace fdt
{
class property;
class node;
/**
 * Type for (owned) pointers to properties.
 */
typedef std::shared_ptr<property> property_ptr;
/**
 * Owning pointer to a node.
 */
typedef std::unique_ptr<node> node_ptr;
/**
 * Map from macros to property pointers.
 */
typedef std::unordered_map<std::string, property_ptr> define_map;
/**
 * Set of strings used for label names.
 */
typedef std::unordered_set<std::string> string_set;
/**
 * Properties may contain a number of different value, each with a different
 * label.  This class encapsulates a single value.
 */
struct property_value
{
	/**
	 * The label for this data.  This is usually empty.
	 */
	std::string label;
	/**
	 * If this value is a string, or something resolved from a string (a
	 * reference) then this contains the source string.
	 */
	std::string string_data;
	/**
	 * The data that should be written to the final output.
	 */
	byte_buffer byte_data;
	/**
	 * Enumeration describing the possible types of a value.  Note that
	 * property-coded arrays will appear simply as binary (or possibly
	 * string, if they happen to be nul-terminated and printable), and must
	 * be checked separately.
	 */
	enum value_type
	{
		/**
		 * This is a list of strings.  When read from source, string
		 * lists become one property value for each string, however
		 * when read from binary we have a single property value
		 * incorporating the entire text, with nul bytes separating the
		 * strings.
		 */
		STRING_LIST,
		/**
		 * This property contains a single string.
		 */
		STRING,
		/**
		 * This is a binary value.  Check the size of byte_data to
		 * determine how many bytes this contains.
		 */
		BINARY,
		/** This contains a short-form address that should be replaced
		 * by a fully-qualified version.  This will only appear when
		 * the input is a device tree source.  When parsed from a
		 * device tree blob, the cross reference will have already been
		 * resolved and the property value will be a string containing
		 * the full path of the target node.  */
		CROSS_REFERENCE,
		/**
		 * This is a phandle reference.  When parsed from source, the
		 * string_data will contain the node label for the target and,
		 * after cross references have been resolved, the binary data
		 * will contain a 32-bit integer that should match the phandle
		 * property of the target node.
		 */
		PHANDLE,
		/**
		 * An empty property value.  This will never appear on a real
		 * property value, it is used by checkers to indicate that no
		 * property values should exist for a property.
		 */
		EMPTY,
		/**
		 * The type of this property has not yet been determined.
		 */
		UNKNOWN
	};
	/**
	 * The type of this property.
	 */
	value_type type;
	/**
	 * Returns true if this value is a cross reference, false otherwise.
	 */
	inline bool is_cross_reference()
	{
		return is_type(CROSS_REFERENCE);
	}
	/**
	 * Returns true if this value is a phandle reference, false otherwise.
	 */
	inline bool is_phandle()
	{
		return is_type(PHANDLE);
	}
	/**
	 * Returns true if this value is a string, false otherwise.
	 */
	inline bool is_string()
	{
		return is_type(STRING);
	}
	/**
	 * Returns true if this value is a string list (a nul-separated
	 * sequence of strings), false otherwise.
	 */
	inline bool is_string_list()
	{
		return is_type(STRING_LIST);
	}
	/**
	 * Returns true if this value is binary, false otherwise.
	 */
	inline bool is_binary()
	{
		return is_type(BINARY);
	}
	/**
	 * Returns this property value as a 32-bit integer.  Returns 0 if this
	 * property value is not 32 bits long.  The bytes in the property value
	 * are assumed to be in big-endian format, but the return value is in
	 * the host native endian.
	 */
	uint32_t get_as_uint32();
	/**
	 * Default constructor, specifying the label of the value.
	 */
	property_value(std::string l=std::string()) : label(l), type(UNKNOWN) {}
	/**
	 * Writes the data for this value into an output buffer.
	 */
	void push_to_buffer(byte_buffer &buffer);

	/**
	 * Writes the property value to the standard output.  This uses the
	 * following heuristics for deciding how to print the output:
	 *
	 * - If the value is nul-terminated and only contains printable
	 *   characters, it is written as a string.
	 * - If it is a multiple of 4 bytes long, then it is printed as cells.
	 * - Otherwise, it is printed as a byte buffer.
	 */
	void write_dts(FILE *file);
	/**
	 * Tries to merge adjacent property values, returns true if it succeeds and
	 * false otherwise.
	 */
	bool try_to_merge(property_value &other);
	/**
	 * Returns the size (in bytes) of this property value.
	 */
	size_t size();
	private:
	/**
	 * Returns whether the value is of the specified type.  If the type of
	 * the value has not yet been determined, then this calculates it.
	 */
	inline bool is_type(value_type v)
	{
		if (type == UNKNOWN)
		{
			resolve_type();
		}
		return type == v;
	}
	/**
	 * Determines the type of the value based on its contents.
	 */
	void resolve_type();
	/**
	 * Writes the property value to the specified file as a quoted string.
	 * This is used when generating DTS.
	 */
	void write_as_string(FILE *file);
	/**
	 * Writes the property value to the specified file as a sequence of
	 * 32-bit big-endian cells.  This is used when generating DTS.
	 */
	void write_as_cells(FILE *file);
	/**
	 * Writes the property value to the specified file as a sequence of
	 * bytes.  This is used when generating DTS.
	 */
	void write_as_bytes(FILE *file);
};

/**
 * A value encapsulating a single property.  This contains a key, optionally a
 * label, and optionally one or more values.
 */
class property
{
	/**
	 * The name of this property.
	 */
	std::string key;
	/**
	 * Zero or more labels.
	 */
	string_set labels;
	/**
	 * The values in this property.
	 */
	std::vector<property_value> values;
	/**
	 * Value indicating that this is a valid property.  If a parse error
	 * occurs, then this value is false.
	 */
	bool valid;
	/**
	 * Parses a string property value, i.e. a value enclosed in double quotes.
	 */
	void parse_string(text_input_buffer &input);
	/**
	 * Parses one or more 32-bit values enclosed in angle brackets.
	 */
	void parse_cells(text_input_buffer &input, int cell_size);
	/**
	 * Parses an array of bytes, contained within square brackets.
	 */
	void parse_bytes(text_input_buffer &input);
	/**
	 * Parses a reference.  This is a node label preceded by an ampersand
	 * symbol, which should expand to the full path to that node.
	 *
	 * Note: The specification says that the target of such a reference is
	 * a node name, however dtc assumes that it is a label, and so we
	 * follow their interpretation for compatibility.
	 */
	void parse_reference(text_input_buffer &input);
	/**
	 * Parse a predefined macro definition for a property.
	 */
	void parse_define(text_input_buffer &input, define_map *defines);
	/**
	 * Constructs a new property from two input buffers, pointing to the
	 * struct and strings tables in the device tree blob, respectively.
	 * The structs input buffer is assumed to have just consumed the
	 * FDT_PROP token.
	 */
	property(input_buffer &structs, input_buffer &strings);
	/**
	 * Parses a new property from the input buffer.  
	 */
	property(text_input_buffer &input,
	         std::string &&k,
	         string_set &&l,
	         bool terminated,
	         define_map *defines);
	public:
	/**
	 * Creates an empty property.
	 */
	property(std::string &&k, string_set &&l=string_set())
		: key(k), labels(l), valid(true) {}
	/**
	 * Copy constructor.
	 */
	property(property &p) : key(p.key), labels(p.labels), values(p.values),
		valid(p.valid) {}
	/**
	 * Factory method for constructing a new property.  Attempts to parse a
	 * property from the input, and returns it on success.  On any parse
	 * error, this will return 0.
	 */
	static property_ptr parse_dtb(input_buffer &structs,
	                              input_buffer &strings);
	/**
	 * Factory method for constructing a new property.  Attempts to parse a
	 * property from the input, and returns it on success.  On any parse
	 * error, this will return 0.
	 */
	static property_ptr parse(text_input_buffer &input,
	                          std::string &&key,
	                          string_set &&labels=string_set(),
	                          bool semicolonTerminated=true,
	                          define_map *defines=0);
	/**
	 * Iterator type used for accessing the values of a property.
	 */
	typedef std::vector<property_value>::iterator value_iterator;
	/**
	 * Returns an iterator referring to the first value in this property.
	 */
	inline value_iterator begin()
	{
		return values.begin();
	}
	/**
	 * Returns an iterator referring to the last value in this property.
	 */
	inline value_iterator end()
	{
		return values.end();
	}
	/**
	 * Adds a new value to an existing property.
	 */
	inline void add_value(property_value v)
	{
		values.push_back(v);
	}
	/**
	 * Returns the key for this property.
	 */
	inline const std::string &get_key()
	{
		return key;
	}
	/**
	 * Writes the property to the specified writer.  The property name is a
	 * reference into the strings table.
	 */
	void write(dtb::output_writer &writer, dtb::string_table &strings);
	/**
	 * Writes in DTS format to the specified file, at the given indent
	 * level.  This will begin the line with the number of tabs specified
	 * as the indent level and then write the property in the most
	 * applicable way that it can determine.
	 */
	void write_dts(FILE *file, int indent);
	/**
	 * Returns the byte offset of the specified property value.
	 */
	size_t offset_of_value(property_value &val);
};

/**
 * Class encapsulating a device tree node.  Nodes may contain properties and
 * other nodes.
 */
class node
{
	public:
	/**
	 * The labels for this node, if any.  Node labels are used as the
	 * targets for cross references.
	 */
	std::unordered_set<std::string> labels;
	/**
	 * The name of the node.
	 */
	std::string name;
	/**
	 * The name of the node is a path reference.
	 */
	bool name_is_path_reference = false;
	/**
	 * The unit address of the node, which is optionally written after the
	 * name followed by an at symbol.
	 */
	std::string unit_address;
	/**
	 * The type for the property vector.
	 */
	typedef std::vector<property_ptr> property_vector;
	/**
	 * Iterator type for child nodes.
	 */
	typedef std::vector<node_ptr>::iterator child_iterator;
	/**
	 * Recursion behavior to be observed for visiting
	 */
	enum visit_behavior
	{
		/**
		 * Recurse as normal through the rest of the tree.
		 */
		VISIT_RECURSE,
		/**
		 * Continue recursing through the device tree, but do not
		 * recurse through this branch of the tree any further.
		 */
		VISIT_CONTINUE,
		/**
		 * Immediately halt the visit.  No further nodes will be visited.
		 */
		VISIT_BREAK
	};
	private:
	/**
	 * Adaptor to use children in range-based for loops.
	 */
	struct child_range
	{
		child_range(node &nd) : n(nd) {}
		child_iterator begin() { return n.child_begin(); }
		child_iterator end() { return n.child_end(); }
		private:
		node &n;
	};
	/**
	 * Adaptor to use properties in range-based for loops.
	 */
	struct property_range
	{
		property_range(node &nd) : n(nd) {}
		property_vector::iterator begin() { return n.property_begin(); }
		property_vector::iterator end() { return n.property_end(); }
		private:
		node &n;
	};
	/**
	 * The properties contained within this node.
	 */
	property_vector props;
	/**
	 * The children of this node.
	 */
	std::vector<node_ptr> children;
	/**
	 * Children that should be deleted from this node when merging.
	 */
	std::unordered_set<std::string> deleted_children;
	/**
	 * Properties that should be deleted from this node when merging.
	 */
	std::unordered_set<std::string> deleted_props;
	/**
	 * A flag indicating whether this node is valid.  This is set to false
	 * if an error occurs during parsing.
	 */
	bool valid;
	/**
	 * Parses a name inside a node, writing the string passed as the last
	 * argument as an error if it fails.  
	 */
	std::string parse_name(text_input_buffer &input,
	                       bool &is_property,
	                       const char *error);
	/**
	 * Constructs a new node from two input buffers, pointing to the struct
	 * and strings tables in the device tree blob, respectively.
	 */
	node(input_buffer &structs, input_buffer &strings);
	/**
	 * Parses a new node from the specified input buffer.  This is called
	 * when the input cursor is on the open brace for the start of the
	 * node.  The name, and optionally label and unit address, should have
	 * already been parsed.
	 */
	node(text_input_buffer &input,
	     std::string &&n,
	     std::unordered_set<std::string> &&l,
	     std::string &&a,
	     define_map*);
	/**
	 * Creates a special node with the specified name and properties.
	 */
	node(const std::string &n, const std::vector<property_ptr> &p);
	/**
	 * Comparison function for properties, used when sorting the properties
	 * vector.  Orders the properties based on their names.
	 */
	static inline bool cmp_properties(property_ptr &p1, property_ptr &p2);
		/*
	{
		return p1->get_key() < p2->get_key();
	}
	*/
	/**
	 * Comparison function for nodes, used when sorting the children
	 * vector.  Orders the nodes based on their names or, if the names are
	 * the same, by the unit addresses.
	 */
	static inline bool cmp_children(node_ptr &c1, node_ptr &c2);
	public:
	/**
	 * Sorts the node's properties and children into alphabetical order and
	 * recursively sorts the children.
	 */
	void sort();
	/**
	 * Returns an iterator for the first child of this node.
	 */
	inline child_iterator child_begin()
	{
		return children.begin();
	}
	/**
	 * Returns an iterator after the last child of this node.
	 */
	inline child_iterator child_end()
	{
		return children.end();
	}
	/**
	 * Returns a range suitable for use in a range-based for loop describing
	 * the children of this node.
	 */
	inline child_range child_nodes()
	{
		return child_range(*this);
	}
	/**
	 * Accessor for the deleted children.
	 */
	inline const std::unordered_set<std::string> &deleted_child_nodes()
	{
		return deleted_children;
	}
	/**
	 * Accessor for the deleted properties
	 */
	inline const std::unordered_set<std::string> &deleted_properties()
	{
		return deleted_props;
	}
	/**
	 * Returns a range suitable for use in a range-based for loop describing
	 * the properties of this node.
	 */
	inline property_range properties()
	{
		return property_range(*this);
	}
	/**
	 * Returns an iterator after the last property of this node.
	 */
	inline property_vector::iterator property_begin()
	{
		return props.begin();
	}
	/**
	 * Returns an iterator for the first property of this node.
	 */
	inline property_vector::iterator property_end()
	{
		return props.end();
	}
	/**
	 * Factory method for constructing a new node.  Attempts to parse a
	 * node in DTS format from the input, and returns it on success.  On
	 * any parse error, this will return 0.  This should be called with the
	 * cursor on the open brace of the property, after the name and so on
	 * have been parsed.
	 */
	static node_ptr parse(text_input_buffer &input,
	                      std::string &&name,
	                      std::unordered_set<std::string> &&label=std::unordered_set<std::string>(),
	                      std::string &&address=std::string(),
	                      define_map *defines=0);
	/**
	 * Factory method for constructing a new node.  Attempts to parse a
	 * node in DTB format from the input, and returns it on success.  On
	 * any parse error, this will return 0.  This should be called with the
	 * cursor on the open brace of the property, after the name and so on
	 * have been parsed.
	 */
	static node_ptr parse_dtb(input_buffer &structs, input_buffer &strings);
	/**
	 * Construct a new special node from a name and set of properties.
	 */
	static node_ptr create_special_node(const std::string &name,
			const std::vector<property_ptr> &props);
	/**
	 * Returns a property corresponding to the specified key, or 0 if this
	 * node does not contain a property of that name.
	 */
	property_ptr get_property(const std::string &key);
	/**
	 * Adds a new property to this node.
	 */
	inline void add_property(property_ptr &p)
	{
		props.push_back(p);
	}
	/**
	 * Adds a new child to this node.
	 */
	inline void add_child(node_ptr &&n)
	{
		children.push_back(std::move(n));
	}
	/**
	 * Merges a node into this one.  Any properties present in both are
	 * overridden, any properties present in only one are preserved.
	 */
	void merge_node(node_ptr &other);
	/**
	 * Write this node to the specified output.  Although nodes do not
	 * refer to a string table directly, their properties do.  The string
	 * table passed as the second argument is used for the names of
	 * properties within this node and its children.
	 */
	void write(dtb::output_writer &writer, dtb::string_table &strings);
	/**
	 * Writes the current node as DTS to the specified file.  The second
	 * parameter is the indent level.  This function will start every line
	 * with this number of tabs.  
	 */
	void write_dts(FILE *file, int indent);
	/**
	 * Recursively visit this node and then its children based on the
	 * callable's return value.  The callable may return VISIT_BREAK
	 * immediately halt all recursion and end the visit, VISIT_CONTINUE to
	 * not recurse into the current node's children, or VISIT_RECURSE to recurse
	 * through children as expected.  parent will be passed to the callable.
	 */
	visit_behavior visit(std::function<visit_behavior(node&, node*)>, node *parent);
};

/**
 * Class encapsulating the entire parsed FDT.  This is the top-level class,
 * which parses the entire DTS representation and write out the finished
 * version.
 */
class device_tree
{
	public:
	/**
	 * Type used for node paths.  A node path is sequence of names and unit
	 * addresses.
	 */
	class node_path : public std::vector<std::pair<std::string,std::string>>
	{
		public:
		/**
		 * Converts this to a string representation.
		 */
		std::string to_string() const;
	};
	/**
	 * Name that we should use for phandle nodes.
	 */
	enum phandle_format
	{
		/** linux,phandle */
		LINUX,
		/** phandle */
		EPAPR,
		/** Create both nodes. */
		BOTH
	};
	private:
	/**
	 * The format that we should use for writing phandles.
	 */
	phandle_format phandle_node_name = EPAPR;
	/**
	 * Flag indicating that this tree is valid.  This will be set to false
	 * on parse errors. 
	 */
	bool valid = true;
	/**
	 * Type used for memory reservations.  A reservation is two 64-bit
	 * values indicating a base address and length in memory that the
	 * kernel should not use.  The high 32 bits are ignored on 32-bit
	 * platforms.
	 */
	typedef std::pair<uint64_t, uint64_t> reservation;
	/**
	 * The memory reserves table.
	 */
	std::vector<reservation> reservations;
	/**
	 * Root node.  All other nodes are children of this node.
	 */
	node_ptr root;
	/**
	 * Mapping from names to nodes.  Only unambiguous names are recorded,
	 * duplicate names are stored as (node*)-1.
	 */
	std::unordered_map<std::string, node*> node_names;
	/**
	 * A map from labels to node paths.  When resolving cross references,
	 * we look up referenced nodes in this and replace the cross reference
	 * with the full path to its target.
	 */
	std::unordered_map<std::string, node_path> node_paths;
	/**
	 * A collection of property values that are references to other nodes.
	 * These should be expanded to the full path of their targets.
	 */
	std::vector<property_value*> cross_references;
	/**
	 * The location of something requiring a fixup entry.
	 */
	struct fixup
	{
		/**
		 * The path to the node.
		 */
		node_path path;
		/**
		 * The property containing the reference.
		 */
		property_ptr prop;
		/**
		 * The property value that contains the reference.
		 */
		property_value &val;
	};
	/**
	 * A collection of property values that refer to phandles.  These will
	 * be replaced by the value of the phandle property in their
	 * destination.
	 */
	std::vector<fixup> fixups;
	/**
	 * The locations of all of the values that are supposed to become phandle
	 * references, but refer to things outside of this file.  
	 */
	std::vector<std::reference_wrapper<fixup>> unresolved_fixups;
	/**
	 * The names of nodes that target phandles.
	 */
	std::unordered_set<std::string> phandle_targets;
	/**
	 * A collection of input buffers that we are using.  These input
	 * buffers are the ones that own their memory, and so we must preserve
	 * them for the lifetime of the device tree.  
	 */
	std::vector<std::unique_ptr<input_buffer>> buffers;
	/**
	 * A map of used phandle values to nodes.  All phandles must be unique,
	 * so we keep a set of ones that the user explicitly provides in the
	 * input to ensure that we don't reuse them.
	 *
	 * This is a map, rather than a set, because we also want to be able to
	 * find phandles that were provided by the user explicitly when we are
	 * doing checking.
	 */
	std::unordered_map<uint32_t, node*> used_phandles;
	/**
	 * Paths to search for include files.  This contains a set of
	 * nul-terminated strings, which are not owned by this class and so
	 * must be freed separately.
	 */
	std::vector<std::string> include_paths;
	/**
	 * Dictionary of predefined macros provided on the command line.
	 */
	define_map               defines;
	/**
	 * The default boot CPU, specified in the device tree header.
	 */
	uint32_t boot_cpu = 0;
	/**
	 * The number of empty reserve map entries to generate in the blob.
	 */
	uint32_t spare_reserve_map_entries = 0;
	/**
	 * The minimum size in bytes of the blob.
	 */
	uint32_t minimum_blob_size = 0;
	/**
	 * The number of bytes of padding to add to the end of the blob.
	 */
	uint32_t blob_padding = 0;
	/**
	 * Is this tree a plugin?
	 */
	bool is_plugin = false;
	/**
	 * Visit all of the nodes recursively, and if they have labels then add
	 * them to the node_paths and node_names vectors so that they can be
	 * used in resolving cross references.  Also collects phandle
	 * properties that have been explicitly added.  
	 */
	void collect_names_recursive(node_ptr &n, node_path &path);
	/**
	 * Assign a phandle property to a single node.  The next parameter
	 * holds the phandle to be assigned, and will be incremented upon
	 * assignment.
	 */
	property_ptr assign_phandle(node *n, uint32_t &next);
	/**
	 * Assign phandle properties to all nodes that have been referenced and
	 * require one.  This method will recursively visit the tree starting at
	 * the node that it is passed.
	 */
	void assign_phandles(node_ptr &n, uint32_t &next);
	/**
	 * Calls the recursive version of this method on every root node.
	 */
	void collect_names();
	/**
	 * Resolves all cross references.  Any properties that refer to another
	 * node must have their values replaced by either the node path or
	 * phandle value.  The phandle parameter holds the next phandle to be
	 * assigned, should the need arise.  It will be incremented upon each
	 * assignment of a phandle.
	 */
	void resolve_cross_references(uint32_t &phandle);
	/**
	 * Parses a dts file in the given buffer and adds the roots to the parsed
	 * set.  The `read_header` argument indicates whether the header has
	 * already been read.  Some dts files place the header in an include,
	 * rather than in the top-level file.
	 */
	void parse_file(text_input_buffer &input,
	                std::vector<node_ptr> &roots,
	                bool &read_header);
	/**
	 * Template function that writes a dtb blob using the specified writer.
	 * The writer defines the output format (assembly, blob).
	 */
	template<class writer>
	void write(int fd);
	public:
	/**
	 * Should we write the __symbols__ node (to allow overlays to be linked
	 * against this blob)?
	 */
	bool write_symbols = false;
	/**
	 * Returns the node referenced by the property.  If this is a tree that
	 * is in source form, then we have a string that we can use to index
	 * the cross_references array and so we can just look that up.  
	 */
	node *referenced_node(property_value &v);
	/**
	 * Writes this FDT as a DTB to the specified output.
	 */
	void write_binary(int fd);
	/**
	 * Writes this FDT as an assembly representation of the DTB to the
	 * specified output.  The result can then be assembled and linked into
	 * a program.
	 */
	void write_asm(int fd);
	/**
	 * Writes the tree in DTS (source) format.
	 */
	void write_dts(int fd);
	/**
	 * Default constructor.  Creates a valid, but empty FDT.
	 */
	device_tree() {}
	/**
	 * Constructs a device tree from the specified file name, referring to
	 * a file that contains a device tree blob.
	 */
	void parse_dtb(const std::string &fn, FILE *depfile);
	/**
	 * Construct a fragment wrapper around node.  This will assume that node's
	 * name may be used as the target of the fragment, and the contents are to
	 * be wrapped in an __overlay__ node.  The fragment wrapper will be assigned
	 * fragnumas its fragment number, and fragment number will be incremented.
	 */
	node_ptr create_fragment_wrapper(node_ptr &node, int &fragnum);
	/**
	 * Generate a root node from the node passed in.  This is sensitive to
	 * whether we're in a plugin context or not, so that if we're in a plugin we
	 * can circumvent any errors that might normally arise from a non-/ root.
	 * fragnum will be assigned to any fragment wrapper generated as a result
	 * of the call, and fragnum will be incremented.
	 */
	node_ptr generate_root(node_ptr &node, int &fragnum);
	/**
	 * Reassign any fragment numbers from this new node, based on the given
	 * delta.
	 */
	void reassign_fragment_numbers(node_ptr &node, int &delta);
	/*
	 * Constructs a device tree from the specified file name, referring to
	 * a file that contains device tree source.
	 */
	void parse_dts(const std::string &fn, FILE *depfile);
	/**
	 * Returns whether this tree is valid.
	 */
	inline bool is_valid()
	{
		return valid;
	}
	/**
	 * Sets the format for writing phandle properties.
	 */
	inline void set_phandle_format(phandle_format f)
	{
		phandle_node_name = f;
	}
	/**
	 * Returns a pointer to the root node of this tree.  No ownership
	 * transfer.
	 */
	inline const node_ptr &get_root() const
	{
		return root;
	}
	/**
	 * Sets the physical boot CPU.
	 */
	void set_boot_cpu(uint32_t cpu)
	{
		boot_cpu = cpu;
	}
	/**
	 * Sorts the tree.  Useful for debugging device trees.
	 */
	void sort()
	{
		if (root)
		{
			root->sort();
		}
	}
	/**
	 * Adds a path to search for include files.  The argument must be a
	 * nul-terminated string representing the path.  The device tree keeps
	 * a pointer to this string, but does not own it: the caller is
	 * responsible for freeing it if required.
	 */
	void add_include_path(const char *path)
	{
		std::string p(path);
		include_paths.push_back(std::move(p));
	}
	/**
	 * Sets the number of empty reserve map entries to add.
	 */
	void set_empty_reserve_map_entries(uint32_t e)
	{
		spare_reserve_map_entries = e;
	}
	/**
	 * Sets the minimum size, in bytes, of the blob.
	 */
	void set_blob_minimum_size(uint32_t s)
	{
		minimum_blob_size = s;
	}
	/**
	 * Sets the amount of padding to add to the blob.
	 */
	void set_blob_padding(uint32_t p)
	{
		blob_padding = p;
	}
	/**
	 * Parses a predefined macro value.
	 */
	bool parse_define(const char *def);
};

} // namespace fdt

} // namespace dtc

#endif // !_FDT_HH_
