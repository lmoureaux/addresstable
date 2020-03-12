#! /usr/bin/env python3

import xml.etree.ElementTree as xml
from rw_reg import parseInt

ADDRESS_TABLE_TOP = 'gem_amc_top.xml'

def nodeDecl(node):
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
        if len(node) > 0:
            decl += nodeStruct(node) + ';\n'
        decl += doc
        decl += nodeType(node) + ' ' + nodeName(node) + ';'
        return decl
    else:
        decl = doc
        if len(node) > 0:
            decl += nodeStruct(node) + ';\n'
        decl += doc
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
    if len(node) == 0:
        raise ValueError('Cannot create node hash for a value node')

    data = ''
    for child in node:
        data += nodeDecl(child) + '\n'
    import hashlib
    return nodeName(node) + '_' + hashlib.md5(data.encode()).hexdigest()[:4]

def nodeStruct(node):
    '''
    Returns the declaration of the struct that corresponds to a node. The node
    must have children.
    '''
    if len(node) == 0:
        raise ValueError('Cannot create node struct for a value node')

    struct = 'struct ' + nodeStructName(node) + ' {\n'
    for child in node:
        struct += nodeDecl(child) + '\n'
    struct += '}'
    return struct

def nodeType(node):
    '''
    Returns the type name for a node. This is the struct name if relevant, and
    is wrapped into 'std::array' for generated registers.
    '''
    baseType = ''
    if len(node) > 0:
        baseType = nodeStructName(node)
    else:
        baseType = 'Register'

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

def nodeValue(node, baseAddress = 0x0):
    '''
    Returns C++ code to initialize a node with the correct addresses.
    'baseAddress' is the address of the parent node.
    '''
    address = (parseInt(node.get('address')) or 0) + baseAddress
    if len(node) == 0:
        addr = hex((address << 2) + 0x64000000) # Real address
        #addr = hex(address) # Logical address
        mask = parseInt(node.get('mask', '0xffffffff'))
        checkMask(mask)
        read = 'r' in node.get('permission', '')
        write = 'w' in node.get('permission', '')
        if mask != 0xffffffff and write and not read:
            raise ValueError(
                'Register {} cannot be mask-written because it cannot be read'.format(nodeName(node)))
        perms = 'true, ' if read else 'false, '
        perms += 'true' if write else 'false'
        return '{{ (std::uint32_t *) {}, {}, {} }}'.format(addr, hex(mask), perms)
    elif node.get('generate') is None:
        value = '{\n'
        for child in node:
            value += nodeValue(child, address) + ',\n'
        value += '}\n'
        return value
    else:
        generateSize = parseInt(node.get('generate_size'))
        generateAddressStep = parseInt(node.get('generate_address_step'))

        value = '{{\n'
        for i in range(generateSize):
            value += '{\n'
            address = baseAddress + generateAddressStep * i
            for child in node:
                value += nodeValue(child, address) + ',\n'
            value += '},\n'
        value += '}}\n'
        return value

tree = xml.parse(ADDRESS_TABLE_TOP)
root = tree.getroot()[0]
print('''
#include <array>

#include "register.h"
''')
print(nodeStruct(root) + ';')
print('const ' + nodeType(root) + ' ' + nodeName(root) + ' = ' + nodeValue(root) + ';')
