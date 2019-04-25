#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>	// for init_task
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cache.h>
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/string_helpers.h>
#include <asm/page.h>
//#include "seq_file.c"

char modname[] = "tasklist";
struct task_struct  *task;
int  n_elt;	

static int hello_proc_show(struct seq_file *m, void *v) {
  seq_printf(m, "Task_List\n");

  return 0;
}

static int hello_proc_open(struct inode *inode, struct  file *file) {
  return single_open(file, hello_proc_show, NULL);
}

static void *seq_buf_alloc(unsigned long size)
{
	return kvmalloc(size, GFP_KERNEL_ACCOUNT);
}

static int traverse(struct seq_file *m, loff_t offset)
{
	loff_t pos = 0;
	int error = 0;
	void *p;

	m->version = 0;
	m->index = 0;
	m->count = m->from = 0;
	if (!offset)
		return 0;

	if (!m->buf) {
		m->buf = seq_buf_alloc(m->size = PAGE_SIZE);
		if (!m->buf)
			return -ENOMEM;
	}
	p = m->op->start(m, &m->index);
	while (p) {
		error = PTR_ERR(p);
		if (IS_ERR(p))
			break;
		error = m->op->show(m, p);
		if (error < 0)
			break;
		if (unlikely(error)) {
			error = 0;
			m->count = 0;
		}
		if (seq_has_overflowed(m))
			goto Eoverflow;
		if (pos + m->count > offset) {
			m->from = offset - pos;
			m->count -= m->from;
			break;
		}
		pos += m->count;
		m->count = 0;
		p = m->op->next(m, p, &m->index);
		if (pos == offset)
			break;
	}
	m->op->stop(m, p);
	return error;

Eoverflow:
	m->op->stop(m, p);
	kvfree(m->buf);
	m->count = 0;
	m->buf = seq_buf_alloc(m->size <<= 1);
	return !m->buf ? -ENOMEM : -EAGAIN;
}


ssize_t seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	size_t copied = 0;
	size_t n;
	void *p;
	int err = 0;

	mutex_lock(&m->lock);

	/*
	 * seq_file->op->..m_start/m_stop/m_next may do special actions
	 * or optimisations based on the file->f_version, so we want to
	 * pass the file->f_version to those methods.
	 *
	 * seq_file->version is just copy of f_version, and seq_file
	 * methods can treat it simply as file version.
	 * It is copied in first and copied out after all operations.
	 * It is convenient to have it as  part of structure to avoid the
	 * need of passing another argument to all the seq_file methods.
	 */
	m->version = file->f_version;

	/*
	 * if request is to read from zero offset, reset iterator to first
	 * record as it might have been already advanced by previous requests
	 */
	if (*ppos == 0) {
		m->index = 0;
		m->version = 0;
		m->count = 0;
	}

	/* Don't assume *ppos is where we left it */
	if (unlikely(*ppos != m->read_pos)) {
		while ((err = traverse(m, *ppos)) == -EAGAIN)
			;
		if (err) {
			/* With prejudice... */
			m->read_pos = 0;
			m->version = 0;
			m->index = 0;
			m->count = 0;
			goto Done;
		} else {
			m->read_pos = *ppos;
		}
	}

	/* grab buffer if we didn't have one */
	if (!m->buf) {
		m->buf = seq_buf_alloc(m->size = PAGE_SIZE);
		if (!m->buf)
			goto Enomem;
	}
	/* if not empty - flush it first */
	if (m->count) {
		n = min(m->count, size);
		err = copy_to_user(buf, m->buf + m->from, n);
		if (err)
			goto Efault;
		m->count -= n;
		m->from += n;
		size -= n;
		buf += n;
		copied += n;
		if (!size)
			goto Done;
	}
	/* we need at least one record in buffer */
	m->from = 0;
	p = m->op->start(m, &m->index);
	while (1) {
		err = PTR_ERR(p);
		if (!p || IS_ERR(p))
			break;
		err = m->op->show(m, p);
		if (err < 0)
			break;
		if (unlikely(err))
			m->count = 0;
		if (unlikely(!m->count)) {
			p = m->op->next(m, p, &m->index);
			continue;
		}
		if (m->count < m->size)
			goto Fill;
		m->op->stop(m, p);
		kvfree(m->buf);
		m->count = 0;
		m->buf = seq_buf_alloc(m->size <<= 1);
		if (!m->buf)
			goto Enomem;
		m->version = 0;
		p = m->op->start(m, &m->index);
	}
	m->op->stop(m, p);
	m->count = 0;
	goto Done;
Fill:
	/* they want more? let's try to get some more */
	while (1) {
		size_t offs = m->count;
		loff_t pos = m->index;

		p = m->op->next(m, p, &m->index);
		if (pos == m->index)
			/* Buggy ->next function */
			m->index++;
		if (!p || IS_ERR(p)) {
			err = PTR_ERR(p);
			break;
		}
		if (m->count >= size)
			break;
		err = m->op->show(m, p);
		if (seq_has_overflowed(m) || err) {
			m->count = offs;
			if (likely(err <= 0))
				break;
		}
	}
	m->op->stop(m, p);
	n = min(m->count, size);
	err = copy_to_user(buf, m->buf, n);
	if (err)
		goto Efault;
	copied += n;
	m->count -= n;
	m->from = n;
Done:
	if (!copied)
		copied = err;
	else {
		*ppos += copied;
		m->read_pos += copied;
	}
	file->f_version = m->version;
	mutex_unlock(&m->lock);
	return copied;
Enomem:
	err = -ENOMEM;
	goto Done;
Efault:
	err = -EFAULT;
	goto Done;
}
EXPORT_SYMBOL(seq_read);

loff_t seq_lseek(struct file *file, loff_t offset, int whence)
{
	struct seq_file *m = file->private_data;
	loff_t retval = -EINVAL;

	mutex_lock(&m->lock);
	m->version = file->f_version;
	switch (whence) {
	case SEEK_CUR:
		offset += file->f_pos;
	case SEEK_SET:
		if (offset < 0)
			break;
		retval = offset;
		if (offset != m->read_pos) {
			while ((retval = traverse(m, offset)) == -EAGAIN)
				;
			if (retval) {
				/* with extreme prejudice... */
				file->f_pos = 0;
				m->read_pos = 0;
				m->version = 0;
				m->index = 0;
				m->count = 0;
			} else {
				m->read_pos = offset;
				retval = file->f_pos = offset;
			}
		} else {
			file->f_pos = offset;
		}
	}
	file->f_version = m->version;
	mutex_unlock(&m->lock);
	return retval;
}
EXPORT_SYMBOL(seq_lseek);

static const struct file_operations hello_proc_fops = {
  .owner = THIS_MODULE,
  .open = hello_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

static int __init hello_proc_init(void) {
  proc_create("task_list", 0, NULL, &hello_proc_fops);
  return 0;
}

static void __exit hello_proc_exit(void) {
  remove_proc_entry("hello_proc", NULL);
}

MODULE_LICENSE("GPL");
module_init(hello_proc_init);
module_exit(hello_proc_exit);
