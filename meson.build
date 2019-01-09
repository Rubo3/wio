project(
	'wio',
	'c',
	license: 'BSD-3-Clause',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

add_project_arguments(
	[
		'-DWL_HIDE_DEPRECATED',
		'-DWLR_USE_UNSTABLE',

		'-Wno-unused-parameter',
		'-Wno-unused-result',
		'-Wundef',
	],
	language: 'c',
)

wlroots = dependency('wlroots', fallback: ['wlroots', 'wlroots'])
wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')

wio_inc = include_directories('include')

subdir('protocols')

wio_sources = files(
	'main.c'
)

executable(
	'wio',
	wio_sources,
	include_directories: [wio_inc],
	dependencies: [
		wlroots,
		wayland_server,
		server_protos,
	],
	install: true
)
