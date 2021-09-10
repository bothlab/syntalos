# Configuration file for the Sphinx documentation builder.

import os
import sys
import textwrap
from breathe.renderer.sphinxrenderer import DomainDirectiveFactory, CMacroObject

# -- Project information -----------------------------------------------------

project = 'Syntalos'
copyright = '2018-2020, Matthias Klumpp'
author = 'Matthias Klumpp'

# The full version, including alpha/beta/rc tags
release = '0.8'

# -- General configuration ---------------------------------------------------
thisfile = __file__
if not os.path.isabs(thisfile):
    thisfile = os.path.normpath(os.path.join(os.getcwd(), thisfile))
project_root = os.path.normpath(os.path.join(os.path.dirname(thisfile), '..'))

html_theme = 'cloud'
html_theme_options = {'borderless_decor': True,
                      'lighter_header_decor': True,
                      'min_height': '16cm',
                      'roottarget': 'index'}

extensions = [
    'breathe',
    'exhale'
]

# FIXME: work around Breathe having issues with Qt properties
DomainDirectiveFactory.cpp_classes['property'] = (CMacroObject, 'macro')

# Setup the breathe extension
breathe_projects = {
    'Syntalos': './doxyoutput/xml'
}
breathe_default_project = 'Syntalos'

# Setup the exhale extension
exhale_args = {
    # These arguments are required
    'containmentFolder':     './api',
    'rootFileName':          'sysrc_root.rst',
    'rootFileTitle':         'Syntalos API',
    'doxygenStripFromPath':  '../src',
    'createTreeView':        True,
    'exhaleExecutesDoxygen': True,
    'exhaleDoxygenStdin':    textwrap.dedent('''
                                INPUT                = ../src/
                                BUILTIN_STL_SUPPORT  = YES
                                EXTRACT_PRIVATE      = NO
                                EXTRACT_PRIV_VIRTUAL = YES
                                EXCLUDE_PATTERNS     = *.txt \
                                                       *.md \
                                                       *.build \
                                                       *.ui \
                                                       */pyworker/cvmatndsliceconvert.cpp
                                INCLUDE_FILE_PATTERNS = *.h *.hpp
                                EXTENSION_MAPPING    = h=C++
                                ENABLE_PREPROCESSING = YES
                                RECURSIVE            = YES
                                EXCLUDE_SYMBOLS      = *::Private \
                                                       moodycamel::* \
                                                       Q_DECLARE_METATYPE \
                                                       Q_DECLARE_FLAGS \
                                                       Q_DECLARE_LOGGING_CATEGORY \
                                                       Q_LOGGING_CATEGORY \
                                                       Q_DECLARE_SMART_POINTER_METATYPE \
                                                       Q_GLOBAL_STATIC_WITH_ARGS
                                EXTRACT_LOCAL_CLASSES  = NO
                                CLANG_ASSISTED_PARSING = YES
                                ''')
}

# Tell sphinx what the primary language being documented is.
primary_domain = 'cpp'

# Tell sphinx what the pygments highlight language should be.
highlight_language = 'cpp'

templates_path = ['_templates']

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']
