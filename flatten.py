#! /usr/bin/env python3

import xml.etree.ElementTree as xml

ADDRESS_TABLE_TOP = 'gem_amc_top.xml'
ALREADY_DECLARED = set()

def parseInt(s):
    if s is None:
        return None
    string = str(s)
    if string.startswith('0x'):
        return int(string, 16)
    elif string.startswith('0b'):
        return int(string, 2)
    else:
        return int(string)

def nodeDecl(node, baseAddress):
    '''
    Returns C++ code to declare a node. This includes the node description as
    Doxygen comments and the node struct.
    '''
    doc = ''
    if node.get('description') is not None:
        doc = '/** \\brief ' + node.get('description') + ' */\n'

    if len(node) == 0:
        return doc + nodeType(node) + ' ' + nodeName(node) + ';'
    elif node.get('generate') is None:
        decl = doc
        decl += nodeType(node) + ' ' + nodeName(node) + ';'
        return decl
    else:
        decl = doc
        decl += nodeType(node) + ' ' + nodeName(node) + ';'
        return decl

def nodeName(node):
    '''
    Returns a C++ name for a node. It is the register name with all generated
    indices removed (including the preceding underscore if any). Leading and
    trailing whitespaces are dropped as well. In case the name starts with a
    digit, 'reg_' is prepended.
    '''
    name = ''
    if node.get('generate') is None:
        name = node.get('id').strip()
    else:
        import re
        # Remove _${counter} or ${counter}
        regex = r'_?\$\{' + node.get('generate_idx_var') + r'\}'
        name = re.sub(regex, '', node.get('id')).strip()

    # Try to turn the name into an identifier. Useful for names starting with
    # digits or with leading/trailing whitespaces
    if not name.isidentifier():
        name = 'reg_' + name
    # Fail if it didn't work
    if not name.isidentifier():
        raise ValueError(
            'Could not convert node id "{}" to a valid C++ identifier'.format(node.get('id')))
    return name

def nodeStructName(node):
    '''
    Returns the name of the struct generated for a node. The node must have
    children. The name is the nodeName() with a hash of the content appended to
    cope with constructs like 'OH.OH'.
    '''
    import hashlib
    return nodeName(node) + '_' + hashlib.md5(xml.tostring(node)).hexdigest()[:4]

def nodeStruct(node, baseAddress):
    '''
    Returns the declaration of the struct that corresponds to a node. The node
    must have children.
    '''
    if len(node) == 0:
        raise ValueError('Cannot create node struct for a value node')

    structName = nodeStructName(node)

    if structName in ALREADY_DECLARED:
        return ''
    else:
        ALREADY_DECLARED.add(structName)

    struct = ''
    for child in node:
        if len(child) > 0:
            struct += nodeStruct(child, baseAddress) + ';\n'

    struct += '''
    template<class ROType = read_only_type,
             class WOType = write_only_type,
             class RWType = read_write_type>
    struct {0} {{
        using read_only_type = ROType;
        using write_only_type = WOType;
        using read_write_type = RWType;

        template<class RO, class WO, class RW> using self_type = {0}<RO, WO, RW>;
        '''.format(structName)

    for child in node:
        struct += nodeDecl(child, baseAddress) + '\n'

    # Address-based constructor
    struct += '''
        constexpr {}(std::uint32_t base = {}):'''.format(
            structName, hex(baseAddress))

    for i in range(len(node)):
        child = node[i]
        struct += '\n          ' + nodeAddrConstructor(child, baseAddress)
        if i != len(node) - 1:
            struct += ','

    struct += '''
    {}''' # Constructor body

    # Generator-based constructor
    struct += '''
        template<
            class Generator,
            /* We take a generic type as a parameter instead of RO, WO, RW to
             * allow for cv qualifiers. Otherwise we would need two constructors. */
            class Other>
        constexpr {0}(Generator &gen, Other &other):'''.format(structName)

    for i in range(len(node)):
        child = node[i]
        struct += '\n          ' + nodeGenConstructor(child)
        if i != len(node) - 1:
            struct += ','

    struct += '''
    {}''' # Constructor body

    struct += '}'
    return struct

def nodeBaseType(node):
    '''
    Returns the base type name for a node. This is the struct name or a generic
    expression, without std::array<> wrapping.
    '''
    if len(node) > 0:
        return nodeStructName(node) + '<read_only_type, write_only_type, read_write_type>'
    else:
        read = 'r' in node.get('permission', '')
        write = 'w' in node.get('permission', '')
        if read and write:
            return 'read_write_type'
        elif read:
            return 'read_only_type'
        elif write:
            return 'write_only_type'
        else:
            raise ValueError('Leaf node {} has no permissions'.format(node.get('id')))

def nodeType(node):
    '''
    Returns the type name for a node. This is the struct name if relevant, and
    is wrapped into 'std::array' for generated registers.
    '''
    baseType = nodeBaseType(node)
    if node.get('generate') is None:
        return baseType
    else:
        return 'std::array<{}, {}>'.format(baseType, node.get('generate_size'))

def checkMask(mask):
    '''
    Checks that a mask is contiguous (it has no 'hole'). Accepts 0b000111000 and
    rejects 0b00101100
    '''
    m = mask
    # Discard trailing zeros
    while m & 0x1 == 0:
        m >>= 1
    # Discard ones
    while m & 0x1 == 1:
        m >>= 1
    # All remaining bits must be 0
    if m != 0:
        raise ValueError('Mask {} has holes'.format(hex(mask)))

def nodeAddrInitializer(node):
    '''
    Constructs the initialization code for this node (used in constructor)
    '''
    address = hex(parseInt(node.get('address')) or 0)
    if len(node) == 0:
        mask = parseInt(node.get('mask', '0xffffffff'))
        checkMask(mask)
        read = 'r' in node.get('permission', '')
        write = 'w' in node.get('permission', '')
        if mask != 0xffffffff and write and not read:
            raise ValueError(
                'Register {} cannot be mask-written because it cannot be read'.format(nodeName(node)))
        if read: # RO and RW
            return 'getAddress(base, {}), {}'.format(address, hex(mask))
        else: # WO, no mask
            return 'getAddress(base, {})'.format(address)
    else:
        return 'base + {}'.format(address)

def nodeAddrConstructor(node, baseAddress):
    '''
    Returns C++ code to initialize a node with the correct addresses.
    'baseAddress' is the address of the parent node.
    '''
    if node.get('generate') is None:
        return '{}({})'.format(nodeName(node), nodeAddrInitializer(node))
    else:
        generateSize = parseInt(node.get('generate_size'))
        generateAddressStep = parseInt(node.get('generate_address_step'))

        value = '{}({{\n'.format(nodeName(node))
        for i in range(generateSize):
            value += '{}({}),\n'.format(
                nodeBaseType(node),
                nodeAddrInitializer(node))
        value += '})\n'
        return value

def nodeGenInitializer(node, otherName):
    '''
    Constructs the initialization code for this node (used in generator-based
    constructor)
    '''
    if len(node) == 0:
        return 'gen({})'.format(otherName)
    else:
        return 'gen, {}'.format(otherName)

def nodeGenConstructor(node):
    '''
    Returns C++ code to initialize a node using a generator.
    '''
    name = nodeName(node)
    otherName = 'other.{}'.format(name)
    if node.get('generate') is None:
        return '{}({})'.format(name, nodeGenInitializer(node, otherName))
    else:
        generateSize = parseInt(node.get('generate_size'))

        value = '{}({{\n'.format(name)
        for i in range(generateSize):
            value += '{}({}),\n'.format(
                nodeBaseType(node),
                nodeGenInitializer(node, '{}[{}]'.format(otherName, i)))
        value += '})\n'
        return value

tree = xml.parse(ADDRESS_TABLE_TOP)
root = tree.getroot()[0]
print('''
#include <array>
#include <type_traits>

#include "register.h"

template<class Generator>
struct GeneratorTraits
{
    template<class Read, class Write>
    using type = decltype(std::declval<Generator>()(
        std::declval<std::uint32_t>(),
        std::declval<std::uint32_t>(),
        std::declval<Read>(),
        std::declval<Write>()));

    using rotype = type<std::true_type, std::false_type>;
    using wotype = type<std::false_type, std::true_type>;
    using rwtype = type<std::true_type, std::true_type>;
};

constexpr std::uint32_t getAddress(std::uint32_t base, std::uint32_t local)
{
    return ((base + local) << 2) + 0x64000000;
}

using read_only_type = const RORegister;
using write_only_type = const WORegister;
using read_write_type = const RWRegister;
''')
print(nodeStruct(root, 0x0) + ';')
print('const constexpr ' + nodeType(root) + ' ' + nodeName(root) + '(0x0);')
