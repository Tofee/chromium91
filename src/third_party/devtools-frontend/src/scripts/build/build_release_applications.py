#!/usr/bin/env vpython
# -*- coding: UTF-8 -*-
#
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Builds applications in release mode:
- Concatenates autostart modules, application modules' module.json descriptors,
and the application loader into a single script.
"""

from cStringIO import StringIO
from os import path
from os.path import join
import copy
import os
import re
import shutil
import sys
import subprocess

from modular_build import read_file, write_file, bail_error
import modular_build
import rjsmin
import special_case_namespaces

try:
    import simplejson as json
except ImportError:
    import json

try:
    original_sys_path = sys.path
    sys.path = sys.path + [path.join(os.path.dirname(os.path.realpath(__file__)), '..')]
    import devtools_paths
finally:
    sys.path = original_sys_path

FRONT_END_DIRECTORY = path.join(os.path.dirname(path.abspath(__file__)), '..', '..', 'front_end')


def main(argv):
    try:
        input_path_flag_index = argv.index('--input_path')
        input_path = argv[input_path_flag_index + 1]
        output_path_gen_flag_index = argv.index('--output_path_gen')
        output_path_gen = argv[output_path_gen_flag_index + 1]
        application_names = argv[1:input_path_flag_index]
        use_rollup = '--rollup' in argv
    except:
        print('Usage: %s app_1 app_2 ... app_N --input_path <input_path> --output_path <output_path> --rollup true' % argv[0])
        raise

    loader = modular_build.DescriptorLoader(input_path)
    for app in application_names:
        descriptors = loader.load_application(app)
        builder = ReleaseBuilder(app, descriptors, input_path, output_path_gen,
                                 use_rollup)
        builder.build_app()


def resource_source_url(url):
    return '\n/*# sourceURL=' + url + ' */'


def minify_js(javascript):
    return rjsmin.jsmin(javascript)


def concatenated_module_filename(module_name, output_dir):
    return join(output_dir,
                module_name + '/' + path.basename(module_name) + '_module.js')


# Outputs:
#   <app_name>.js
#   <module_name>_module.js
class ReleaseBuilder(object):

    def __init__(self, application_name, descriptors, application_dir,
                 output_path_gen_dir, use_rollup):
        self.application_name = application_name
        self.descriptors = descriptors
        self.application_dir = application_dir
        self.output_path_gen_dir = output_path_gen_dir
        self.use_rollup = use_rollup
        self._special_case_namespaces = special_case_namespaces.special_case_namespaces

    def app_file(self, extension):
        return self.application_name + '.' + extension

    def autorun_resource_names(self):
        result = []
        for module in self.descriptors.sorted_modules():
            if self.descriptors.application[module].get('type') != 'autostart':
                continue

            resources = self.descriptors.modules[module].get('resources')
            if not resources:
                continue
            for resource_name in resources:
                result.append(path.join(module, resource_name))
        return result

    def build_app(self):
        self._build_app_script()
        for module in filter(lambda desc: (not desc.get('type')),
                             self.descriptors.application.values()):
            self._concatenate_dynamic_module(module['name'])

    def _build_app_script(self):
        script_name = self.app_file('js')
        output = StringIO()
        self._concatenate_application_script(output)
        minified_content = minify_js(output.getvalue())
        write_file(join(self.output_path_gen_dir, script_name),
                   minified_content)
        output.close()

    def _release_module_descriptors(self):
        module_descriptors = self.descriptors.modules
        result = []
        for name in module_descriptors:
            module = copy.copy(module_descriptors[name])
            module_type = self.descriptors.application[name].get('type')
            resources = module.get('resources', None)
            if resources:
                # Resources are already baked into _module.
                del module['resources']
                if not module_type == 'autostart':
                    # We load the entrypoint of a module no matter what.
                    # Therefore, we don't need to declare any files for
                    # the default case. However, if a module still has
                    # a legacy file, the Runtime performs an array
                    # contains check and will load that instead.
                    module_files_to_load = []
                    declared_module_files = module.get('modules', [])
                    legacyFileName = path.basename(name) + '-legacy.js'
                    if legacyFileName in declared_module_files:
                        module_files_to_load += [legacyFileName]
                    # Non-autostart modules are vulcanized.
                    module['modules'] = [path.basename(name) + '_module.js'
                                         ] + module_files_to_load
            result.append(module)
        return json.dumps(result)

    def _write_module_resources(self, resource_names, output):
        for resource_name in resource_names:
            resource_name = path.normpath(resource_name).replace('\\', '/')
            output.write('RootModule.Runtime.cachedResources.set("%s", "' %
                         resource_name)
            resource_content = read_file(path.join(self.application_dir, resource_name))
            if not (resource_name.endswith('.html')
                    or resource_name.endswith('md')):
                resource_content += resource_source_url(resource_name).encode(
                    'utf-8')
            resource_content = resource_content.replace('\\', '\\\\')
            resource_content = resource_content.replace('\n', '\\n')
            resource_content = resource_content.replace('"', '\\"')
            output.write(resource_content)
            output.write('");\n')

    def _concatenate_autostart_modules(self, output):
        non_autostart = set()
        sorted_module_names = self.descriptors.sorted_modules()
        for name in sorted_module_names:
            desc = self.descriptors.modules[name]
            name = desc['name']
            type = self.descriptors.application[name].get('type')
            if type == 'autostart':
                deps = set(desc.get('dependencies', []))
                non_autostart_deps = deps & non_autostart
                if len(non_autostart_deps):
                    bail_error(
                        'Non-autostart dependencies specified for the autostarted module "%s": %s' % (name, non_autostart_deps))
            else:
                non_autostart.add(name)

    def _concatenate_application_script(self, output):
        output.write('Root.allDescriptors.push(...%s);' % self._release_module_descriptors())
        if self.descriptors.extends:
            output.write('Root.applicationDescriptor.modules.push(...%s);' % json.dumps(self.descriptors.application.values()))
        else:
            output.write('Root.applicationDescriptor = %s;' % self.descriptors.application_json())

        output.write("import * as RootModule from './core/root/root.js';")
        self._write_module_resources(self.autorun_resource_names(), output)

        output.write(minify_js(read_file(join(self.application_dir, self.app_file('js')))))
        self._concatenate_autostart_modules(output)

    def _concatenate_dynamic_module(self, module_name):
        module = self.descriptors.modules[module_name]
        modules = module.get('modules')
        resources = self.descriptors.module_resources(module_name)
        module_dir = join(self.application_dir, module_name)
        output = StringIO()
        if resources:
            relative_file_name = '../core/root/root.js'
            if "/" in module_name:
                relative_file_name = '../' + relative_file_name
            output.write("import * as RootModule from '%s';" %
                         relative_file_name)
            self._write_module_resources(resources, output)
        minified_content = minify_js(output.getvalue())
        write_file(
            concatenated_module_filename(module_name,
                                         self.output_path_gen_dir),
            minified_content)
        output.close()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
