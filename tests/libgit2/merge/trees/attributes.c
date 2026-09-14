#include "clar_libgit2.h"
#include "git2/merge.h"
#include "repository.h"
#include "sysdir.h"
#include "../merge_helpers.h"

static git_repository *repo;

void test_merge_trees_attributes__initialize(void)
{
	git_str path = GIT_STR_INIT;

	repo = NULL;
	cl_git_pass(p_mkdir("merge-attributes-system", 0777));
	cl_git_pass(git_str_joinpath(&path, clar_sandbox_path(),
		"merge-attributes-system"));
	cl_git_pass(git_sysdir_set(GIT_SYSDIR_SYSTEM, path.ptr));
	git_str_dispose(&path);
}

void test_merge_trees_attributes__cleanup(void)
{
	cl_git_sandbox_cleanup();
	cl_sandbox_set_search_path_defaults();
	cl_git_pass(p_unlink("merge-attributes-system/gitattributes"));
	cl_git_pass(p_rmdir("merge-attributes-system"));
}

static void system_attributes(const char *contents)
{
	cl_git_rewritefile("merge-attributes-system/gitattributes", contents);
}

static git_index *merge(const char *ours, const char *theirs, bool skip)
{
	git_index *index;
	git_merge_options opts = GIT_MERGE_OPTIONS_INIT;

	if (skip)
		opts.flags |= GIT_MERGE_SKIP_ATTRIBUTES;
	cl_git_pass(merge_commits_from_branches(&index, repo, ours, theirs, &opts));
	return index;
}

void test_merge_trees_attributes__skips_source_loading_and_warm_cache(void)
{
	git_index *index;
	git_config *config;
	git_str user_path = GIT_STR_INIT;

	repo = cl_git_sandbox_init("merge-resolve");
	system_attributes("[attr]systemmacro merge=union\n* systemmacro\n");
	cl_git_rewritefile("merge-resolve/.gitattributes", "* merge=union\n");
	cl_git_pass(p_mkdir("merge-resolve/.git/info", 0777));
	cl_git_rewritefile("merge-resolve/.git/info/attributes", "* merge=union\n");
	cl_git_rewritefile("merge-resolve/user-attributes", "* merge=union\n");
	cl_git_pass(git_str_joinpath(&user_path, git_repository_workdir(repo),
		"user-attributes"));
	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_string(config, "core.attributesfile", user_path.ptr));
	git_config_free(config);
	git_str_dispose(&user_path);

	/* No cache initialization means no source discovery or macro preload. */
	cl_assert(git_repository_attr_cache(repo) == NULL);
	index = merge("master", "branch", true);
	cl_assert(git_index_get_bypath(index, "conflicting.txt", 1) != NULL);
	cl_assert(git_index_get_bypath(index, "automergeable.txt", 0) != NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);

	/* The same sources affect ordinary merging and populate its cache. */
	index = merge("master", "branch", false);
	cl_assert(!git_index_has_conflicts(index));
	cl_assert(git_attr_cache__lookup_macro(repo, "systemmacro") != NULL);
	cl_assert(git_attr_cache__is_cached(repo, GIT_ATTR_FILE_SOURCE_FILE,
		".git/info/attributes"));
	git_index_free(index);

	/* Opting out neither uses nor clears the other operation's cache. */
	index = merge("master", "branch", true);
	cl_assert(git_index_get_bypath(index, "conflicting.txt", 1) != NULL);
	cl_assert(git_attr_cache__lookup_macro(repo, "systemmacro") != NULL);
	git_index_free(index);
	index = merge("master", "branch", false);
	cl_assert(!git_index_has_conflicts(index));
	git_index_free(index);
}

void test_merge_trees_attributes__ignores_system_binary_selection(void)
{
	git_index *index;

	repo = cl_git_sandbox_init("merge-resolve");
	system_attributes("* -merge\n");
	index = merge("master", "branch", true);
	cl_assert(git_index_get_bypath(index, "automergeable.txt", 0) != NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);
	index = merge("master", "branch", false);
	cl_assert(git_index_get_bypath(index, "automergeable.txt", 1) != NULL);
	git_index_free(index);
}

void test_merge_trees_attributes__ignores_default_driver(void)
{
	git_index *index;
	git_config *config;

	repo = cl_git_sandbox_init("merge-resolve");
	system_attributes("");
	cl_git_pass(git_repository_config(&config, repo));
	cl_git_pass(git_config_set_string(config, "merge.default", "union"));
	git_config_free(config);
	index = merge("master", "branch", true);
	cl_assert(git_index_get_bypath(index, "conflicting.txt", 1) != NULL);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);
	index = merge("master", "branch", false);
	cl_assert(!git_index_has_conflicts(index));
	git_index_free(index);
}

void test_merge_trees_attributes__recursive_bases_preserve_opt_out(void)
{
	git_index *index;
	git_oid expected;
	const git_index_entry *entry;

	repo = cl_git_sandbox_init("merge-recursive");
	system_attributes("* merge=union\n");
	index = merge("branchC-1", "branchC-2", true);
	cl_assert(!git_index_has_conflicts(index));
	cl_assert_equal_sz(6, git_index_entrycount(index));
	entry = git_index_get_bypath(index, "veal.txt", 0);
	cl_assert(entry != NULL);
	cl_git_pass(git_oid__fromstr(&expected,
		"15faa0c9991f2d65686e844651faa2ff9827887b", GIT_OID_SHA1));
	cl_assert_equal_oid(&expected, &entry->id);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);
	index = merge("branchC-1", "branchC-2", false);
	cl_assert(git_repository_attr_cache(repo) != NULL);
	git_index_free(index);
}

void test_merge_trees_attributes__rename_similarity_does_not_load_attributes(void)
{
	git_index *index;
	const git_index_entry *entry;
	git_oid expected;

	repo = cl_git_sandbox_init("merge-resolve");
	system_attributes("* merge=union diff=custom\n");
	index = merge("rename_conflict_ours", "rename_conflict_theirs", true);
	entry = git_index_get_bypath(index,
		"1a-newname-in-ours-edited-in-theirs.txt", 0);
	cl_assert(entry != NULL);
	cl_git_pass(git_oid__fromstr(&expected,
		"0d872f8e871a30208305978ecbf9e66d864f1638", GIT_OID_SHA1));
	cl_assert_equal_oid(&expected, &entry->id);
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);
}

static git_tree *binary_tree(char value)
{
	git_treebuilder *builder;
	git_oid blob, tree;
	git_tree *result;
	char contents[] = { 0, value };

	cl_git_pass(git_blob_create_from_buffer(&blob, repo, contents, sizeof(contents)));
	cl_git_pass(git_treebuilder_new(&builder, repo, NULL));
	cl_git_pass(git_treebuilder_insert(NULL, builder, "binary", &blob, GIT_FILEMODE_BLOB));
	cl_git_pass(git_treebuilder_write(&tree, builder));
	git_treebuilder_free(builder);
	cl_git_pass(git_tree_lookup(&result, repo, &tree));
	return result;
}

void test_merge_trees_attributes__preserves_binary_content_and_file_favor(void)
{
	git_tree *base, *ours, *theirs;
	git_index *index;
	git_merge_options opts = GIT_MERGE_OPTIONS_INIT;
	const git_index_entry *entry;
	const git_tree_entry *our_entry;

	repo = cl_git_sandbox_init("merge-resolve");
	system_attributes("* merge=union\n");
	base = binary_tree('a');
	ours = binary_tree('b');
	theirs = binary_tree('c');
	opts.flags |= GIT_MERGE_SKIP_ATTRIBUTES;
	cl_git_pass(git_merge_trees(&index, repo, base, ours, theirs, &opts));
	cl_assert(git_index_has_conflicts(index));
	cl_assert(git_repository_attr_cache(repo) == NULL);
	git_index_free(index);

	opts.file_favor = GIT_MERGE_FILE_FAVOR_OURS;
	cl_git_pass(git_merge_trees(&index, repo, base, ours, theirs, &opts));
	cl_assert(!git_index_has_conflicts(index));
	entry = git_index_get_bypath(index, "binary", 0);
	our_entry = git_tree_entry_byname(ours, "binary");
	cl_assert(entry != NULL && our_entry != NULL);
	cl_assert_equal_oid(&entry->id, git_tree_entry_id(our_entry));
	git_index_free(index);
	git_tree_free(base);
	git_tree_free(ours);
	git_tree_free(theirs);
}
