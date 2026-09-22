#include "clar_libgit2.h"
#include "attr_file.h"

static git_repository *repo;

void test_ignore_captured__initialize(void)
{
	repo = cl_git_sandbox_init("attr");
}

void test_ignore_captured__cleanup(void)
{
	cl_git_sandbox_cleanup();
}

void test_ignore_captured__context_absence_and_repeated_lookup(void)
{
	git_ignore_source *nested, *global, *empty;
	const char *rules = "/root-only\n";
	int ignored, i;

	cl_git_pass(git_ignore_source_new(
		&nested, repo, "sub/.gitignore", rules, strlen(rules)));
	cl_git_pass(git_ignore_source_new(
		&global, repo, "sub/rules", rules, strlen(rules)));
	cl_git_pass(git_ignore_source_new(
		&empty, repo, "", NULL, 0));

	for (i = 0; i < 3; i++) {
		cl_git_pass(git_ignore_source_path_is_ignored(
			&ignored, nested, "root-only"));
		cl_assert_equal_i(0, ignored);
		cl_git_pass(git_ignore_source_path_is_ignored(
			&ignored, nested, "sub/root-only"));
		cl_assert_equal_i(1, ignored);
		cl_git_pass(git_ignore_source_path_is_ignored(
			&ignored, global, "root-only"));
		cl_assert_equal_i(1, ignored);
		cl_git_pass(git_ignore_source_path_is_ignored(
			&ignored, empty, "root-only"));
		cl_assert_equal_i(0, ignored);
	}

	git_ignore_source_free(nested);
	git_ignore_source_free(global);
	git_ignore_source_free(empty);
	git_ignore_source_free(NULL);
}

void test_ignore_captured__rejects_oversized_without_reading(void)
{
	git_ignore_source *source = NULL;
	cl_git_fail_with(GIT_EINVALID, git_ignore_source_new(
		&source, repo, "rules", "x", GIT_ATTR_MAX_FILE_SIZE + 1));
	cl_assert_equal_p(NULL, source);
}
