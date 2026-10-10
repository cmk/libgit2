#include "clar_libgit2.h"
#include "attr.h"
#include "repository.h"
#include "sysdir.h"

static git_repository *repo;
static git_attr_read_session *session;
static git_str system_path = GIT_STR_INIT;

void test_attr_session__initialize(void)
{
	git_config *config;

	repo = cl_git_sandbox_init("empty_standard_repo");
	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_string(config, "core.attributesfile", ""));
	cl_git_pass(git_config_set_string(config, "core.excludesfile", ""));
	git_config_free(config);
	cl_git_mkfile("session-system", "[attr]guard filter=captured\nplain filter=system\n");
	cl_git_pass(git_fs_path_prettify(&system_path, "session-system", NULL));
	cl_git_mkfile("empty_standard_repo/.gitattributes", "file guard\nfolder/ filter=directory\n");
}

void test_attr_session__cleanup(void)
{
	git_attr_read_session_free(session);
	session = NULL;
	git_str_dispose(&system_path);
	cl_git_sandbox_cleanup();
	cl_sandbox_set_search_path_defaults();
	if (git_fs_path_exists("session-system"))
		cl_git_pass(p_unlink("session-system"));
}

static const char *query(const char *path, int is_dir)
{
	const char *value;
	cl_git_pass(git_attr_read_session_get(&value, session, path, is_dir, "filter"));
	return value;
}

void test_attr_session__outstanding_index_cannot_reload_ambient_macros(void)
{
	git_index *index;
	git_str root = GIT_STR_INIT;
	cl_git_mkfile("empty_standard_repo/gitattributes", "[attr]guard filter=ambient\n");
	cl_git_mkfile("empty_standard_repo/file", "content\n");
	cl_git_pass(p_mkdir("empty_standard_repo/nested", 0777));
	cl_git_mkfile("empty_standard_repo/nested/.gitattributes", "file guard\n");
	cl_git_pass(git_fs_path_prettify_dir(&root, "empty_standard_repo", NULL));
	cl_git_pass(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, root.ptr));
	git_str_dispose(&root);
	cl_git_pass(git_repository_index(&index, repo));
	cl_git_fail(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_assert(session == NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	cl_git_pass(git_index_add_bypath(index, "file"));
	git_attr_cache_flush(repo);
	git_index_free(index);
	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("nested/file", 0)));
}

void test_attr_session__required_contents_survive_removal(void)
{
	cl_git_pass(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_git_pass(p_unlink(system_path.ptr));
	cl_assert_equal_s("captured", query("file", 0));
	cl_assert_equal_s("system", query("plain", 0));
	git_attr_read_session_free(session);
	session = NULL;
	cl_assert(git_repository_attr_cache(repo) == NULL);
	cl_git_fail(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_assert(session == NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("file", 0)));
}

void test_attr_session__system_scope_is_independent_of_worktree_location(void)
{
	git_str nested = GIT_STR_INIT;
	cl_git_pass(p_mkdir("empty_standard_repo/policy", 0777));
	cl_git_mkfile("empty_standard_repo/policy/.gitattributes", "/file.txt filter=system\n");
	cl_git_pass(git_fs_path_prettify(&nested, "empty_standard_repo/policy/.gitattributes", NULL));
	cl_git_pass(git_attr_read_session_new(&session, repo, nested.ptr, 0));
	git_str_dispose(&nested);
	cl_assert_equal_s("system", query("file.txt", 0));
	cl_assert_equal_s("system", query("policy/file.txt", 0));
	cl_assert_equal_s("system", query("file.txt", 0));
}

void test_attr_session__posix_colon_names_remain_relative_queries(void)
{
#ifndef GIT_WIN32
	cl_git_pass(p_mkdir("empty_standard_repo/C:", 0777));
	cl_git_pass(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_assert_equal_s("system", query("C:/plain", 0));
#else
	cl_skip();
#endif
}

void test_attr_session__no_system_flag_keeps_explicit_macro_preload(void)
{
	cl_git_pass(git_attr_read_session_new(&session, repo, system_path.ptr, GIT_ATTR_CHECK_NO_SYSTEM));
	cl_assert_equal_s("captured", query("file", 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("plain", 0)));
}

void test_attr_session__absence_never_searches_system_directory(void)
{
	git_str root = GIT_STR_INIT;
	cl_git_mkfile("empty_standard_repo/gitattributes", "[attr]guard filter=ambient\nplain filter=ambient\n");
	cl_git_pass(git_fs_path_prettify_dir(&root, "empty_standard_repo", NULL));
	cl_git_pass(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, root.ptr));
	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("file", 0)));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("plain", 0)));
	git_attr_read_session_free(session);
	session = NULL;
	cl_git_pass(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_assert_equal_s("captured", query("file", 0));
	cl_assert_equal_s("system", query("plain", 0));
	git_str_dispose(&root);
}

void test_attr_session__kind_is_explicit_even_when_operand_is_missing(void)
{
	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("folder", 0)));
	cl_assert_equal_s("directory", query("folder", 1));
}

void test_attr_session__contaminated_cache_is_not_destroyed(void)
{
	const char *value;
	cl_git_pass(git_attr_add_macro(repo, "guard", "filter=existing"));
	cl_git_fail(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_assert(session == NULL);
	cl_git_pass(git_attr_get(&value, repo, 0, "file", "filter"));
	cl_assert_equal_s("existing", value);
}

void test_attr_session__invalid_options_and_queries_fail_cleanly(void)
{
	const char *value;
	cl_git_fail(git_attr_read_session_new(&session, repo, "relative", 0));
	cl_assert(session == NULL);
	cl_git_fail(git_attr_read_session_new(&session, repo, NULL, 3));
	cl_assert(session == NULL);
	cl_git_fail(git_attr_read_session_new(&session, repo, NULL, GIT_ATTR_CHECK_INCLUDE_COMMIT));
	cl_assert(session == NULL);
	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	cl_git_fail(git_attr_read_session_get(&value, session, system_path.ptr, 0, "filter"));
	cl_assert(value == NULL);
	cl_git_fail(git_attr_read_session_get(&value, session, "file", 2, "filter"));
	cl_assert(value == NULL);
	cl_assert_equal_s("directory", query("folder", 1));
}

void test_attr_session__windows_source_and_query_roots(void)
{
#ifdef GIT_WIN32
	const char *unc[] = { "//server/share/attrs", "\\\\server\\share\\attrs", "//?/C:/attrs" };
	const char *relative[] = { "/attrs", "\\attrs", "C:attrs" };
	const char *value;
	git_attr_read_session *other;
	size_t i;

	cl_git_pass(git_attr_read_session_new(&session, repo, NULL, 0));
	for (i = 0; i < ARRAY_SIZE(unc); i++) {
		/* Cache refusal proves syntax admission without contacting a share. */
		cl_git_fail(git_attr_read_session_new(&other, repo, unc[i], 0));
		cl_assert(other == NULL);
		cl_assert_equal_s("attribute session requires a fresh cache", git_error_last()->message);
		cl_git_fail(git_attr_read_session_get(&value, session, unc[i], 0, "filter"));
		cl_assert(value == NULL);
		cl_assert_equal_s("invalid attribute session query", git_error_last()->message);
	}
	for (i = 0; i < ARRAY_SIZE(relative); i++) {
		cl_git_fail(git_attr_read_session_new(&other, repo, relative[i], 0));
		cl_assert(other == NULL);
		cl_assert_equal_s("invalid attribute session options", git_error_last()->message);
	}
#else
	cl_skip();
#endif
}

void test_attr_session__required_nonregular_sources_are_not_absence(void)
{
	cl_git_fail(git_attr_read_session_new(&session, repo, git_repository_workdir(repo), 0));
	cl_assert(session == NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
#ifndef GIT_WIN32
	cl_git_fail(git_attr_read_session_new(&session, repo, "/dev/null", 0));
	cl_assert(session == NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
#endif
	cl_git_mkfile("session-system", "");
	cl_git_pass(git_attr_read_session_new(&session, repo, system_path.ptr, 0));
	cl_assert(GIT_ATTR_IS_UNSPECIFIED(query("plain", 0)));
}
