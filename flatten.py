#! /usr/bin/env python3

import xml.etree.ElementTree as xml

ADDRESS_TABLE_TOP = 'gem_amc_top.xml'

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
        return doc + nodeType(node, baseAddress) + ' ' + nodeName(node) + ';'
    elif node.get('generate') is None:
        decl = doc
        if len(node) > 0:
            decl += nodeStruct(node, baseAddress) + ';\n'
        decl += doc
        decl += nodeType(node, baseAddress) + ' ' + nodeName(node) + ';'
        return decl
    else:
        decl = doc
        if len(node) > 0:
            decl += nodeStruct(node, baseAddress) + ';\n'
        decl += doc
        decl += nodeType(node, baseAddress) + ' ' + nodeName(node) + ';'
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

def nodeStructName(node, baseAddress):
    '''
    Returns the name of the struct generated for a node. The node must have
    children. The name is the nodeName() with a hash of the content appended to
    cope with constructs like 'OH.OH'.
    '''
    import hashlib
    return nodeName(node) + '_' + hashlib.md5(node.text.encode()).hexdigest()[:4]

def nodeStruct(node, baseAddress):
    '''
    Returns the declaration of the struct that corresponds to a node. The node
    must have children.
    '''
    if len(node) == 0:
        raise ValueError('Cannot create node struct for a value node')

    structName = nodeStructName(node, baseAddress)
    struct = '''
    template<class {0}Generator = _M_Generator>
    struct {0} {{
        using _M_Generator = {0}Generator;
        template<class T> using _M_self = {0}<T>;
        '''.format(structName)

    for child in node:
        struct += nodeDecl(child, baseAddress) + '\n'

    # Constructor
    struct += '''
        constexpr {}(_M_Generator &gen, std::uint32_t base = {}):'''.format(
            structName, hex(baseAddress))

    for i in range(len(node)):
        child = node[i]
        struct += '\n          ' + nodeConstructor(child, baseAddress)
        '''
            {}(gen)'''.format(nodeName(child))
        if i != len(node) - 1:
            struct += ','

    struct += '''
    {}''' # Constructor body

    struct += '}'
    return struct

def nodeBaseType(node, baseAddress):
    '''
    Returns the base type name for a node. This is the struct name or a generic
    expression, without std::array<> wrapping.
    '''
    if len(node) > 0:
        return nodeStructName(node, baseAddress) + '<>'
    else:
        return 'typename GeneratorTraits<_M_Generator>::type'

def nodeType(node, baseAddress):
    '''
    Returns the type name for a node. This is the struct name if relevant, and
    is wrapped into 'std::array' for generated registers.
    '''
    baseType = nodeBaseType(node, baseAddress)
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

def nodeInitializer(node, baseAddress):
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
        perms = 'true, ' if read else 'false, '
        perms += 'true' if write else 'false'
        return 'gen(getAddress(base, {}), {}, {})'.format(
            address, hex(mask), perms)
    else:
        return 'gen, base + {}'.format(address)

def nodeConstructor(node, baseAddress):
    '''
    Returns C++ code to initialize a node with the correct addresses.
    'baseAddress' is the address of the parent node.
    '''
    if node.get('generate') is None:
        return '{}({})'.format(nodeName(node), nodeInitializer(node, baseAddress))
    else:
        generateSize = parseInt(node.get('generate_size'))
        generateAddressStep = parseInt(node.get('generate_address_step'))

        value = '{}({{\n'.format(nodeName(node))
        for i in range(generateSize):
            value += '{}({}),\n'.format(
                nodeBaseType(node, baseAddress),
                nodeInitializer(node, baseAddress + generateAddressStep * i))
        value += '})\n'
        return value

tree = xml.parse(ADDRESS_TABLE_TOP)
root = tree.getroot()[0]
print('''
#include <array>

#include "register.h"

template<class Generator>
struct GeneratorTraits
{
    using type = decltype(std::declval<Generator>()(
        std::declval<std::uint32_t>(),
        std::declval<std::uint32_t>(),
        std::declval<bool>(),
        std::declval<bool>()));
};

constexpr std::uint32_t getAddress(std::uint32_t base, std::uint32_t local)
{
    return ((base + local) << 2) + 0x64000000;
}
''')
print('using _M_Generator = const RegisterGenerator;')
print('const constexpr _M_Generator gen;')
print(nodeStruct(root, 0x0) + ';')
print('const constexpr ' + nodeType(root, 0x0) + ' ' + nodeName(root) + '(gen, 0x0);')
